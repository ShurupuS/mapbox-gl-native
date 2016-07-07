#include "node_request.hpp"
#include <mbgl/storage/response.hpp>
#include <mbgl/util/chrono.hpp>

#include <cmath>
#include <iostream>

namespace node_mbgl {

NodeRequest::NodeRequest(
    NodeMap* target_,
    mbgl::FileSource::Callback fileSourceCallback_)
    : AsyncWorker(nullptr),
    target(target_),
    fileSourceCallback(std::make_unique<mbgl::FileSource::Callback>(fileSourceCallback_)) {
    Nan::HandleScope scope;

    auto fn = Nan::New(handleCallback);

    // Bind a reference to this object on the callback function
    fn->SetHiddenValue(Nan::New("worker").ToLocalChecked(), Nan::New<v8::External>(this));

    callback.Reset(fn);
}

NodeRequest::~NodeRequest() {
    std::cout << "~NodeRequest" << std::endl;

    // When this object gets destroyed, make sure that the
    // AsyncRequest can no longer attempt to remove the callback function
    // this object was holding (it can't be fired anymore).
    if (asyncRequest) {
        asyncRequest->worker = nullptr;
    }
}

Nan::Persistent<v8::Function> NodeRequest::constructor;

NAN_MODULE_INIT(NodeRequest::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);

    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("Request").ToLocalChecked());

    constructor.Reset(tpl->GetFunction());
    Nan::Set(target, Nan::New("Request").ToLocalChecked(), tpl->GetFunction());
}

void NodeRequest::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Nan::HandleScope scope;

    auto target = reinterpret_cast<NodeMap*>(info[0].As<v8::External>()->Value());
    auto callback = reinterpret_cast<mbgl::FileSource::Callback*>(info[1].As<v8::External>()->Value());

    auto req = new NodeRequest(target, *callback);
    req->Wrap(info.This());

    info.GetReturnValue().Set(info.This());
}

void NodeRequest::Execute() {
    Nan::HandleScope scope;

    v8::Local<v8::Value> argv[] = {
        handle(),
        Nan::New(callback)
    };

    Nan::MakeCallback(Nan::To<v8::Object>(target->handle()->GetInternalField(1)).ToLocalChecked(), "request", 2, argv);
}

void NodeRequest::WorkComplete() {
    // If callback has already been called, no-op
    if (!fileSourceCallback) return;

    ErrorMessage() ? HandleErrorCallback() : HandleOKCallback();
}

void NodeRequest::HandleCallback(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto hiddenValue = info.Callee()->GetHiddenValue(Nan::New("worker").ToLocalChecked());
    auto external = hiddenValue.As<v8::External>();
    auto externalValue = external->Value();
    auto worker = reinterpret_cast<NodeRequest*>(externalValue);

    if (info[0]->IsObject()) {
        auto err = info[0]->ToObject();
        auto msg = Nan::New("message").ToLocalChecked();

        if (Nan::Has(err, msg).IsJust()) {
            worker->SetErrorMessage(*Nan::Utf8String(
                Nan::Get(err, msg).ToLocalChecked()));
        }
    } else if (info[0]->IsString()) {
        worker->SetErrorMessage(*Nan::Utf8String(info[0]));
    } else if (info.Length() < 1) {
        worker->response.noContent = true;
    } else if (info.Length() < 2 || !info[1]->IsObject()) {
        return Nan::ThrowTypeError("Second argument must be a response object");
    } else {
        auto res = info[1]->ToObject();

        if (Nan::Has(res, Nan::New("modified").ToLocalChecked()).IsJust()) {
            const double modified = Nan::To<double>(Nan::Get(res, Nan::New("modified").ToLocalChecked()).ToLocalChecked()).FromJust();
            if (!std::isnan(modified)) {
                worker->response.modified = mbgl::Timestamp { mbgl::Seconds(
                    static_cast<mbgl::Seconds::rep>(modified / 1000)) };
            }
        }

        if (Nan::Has(res, Nan::New("expires").ToLocalChecked()).IsJust()) {
            const double expires = Nan::To<double>(Nan::Get(res, Nan::New("expires").ToLocalChecked()).ToLocalChecked()).FromJust();
            if (!std::isnan(expires)) {
                worker->response.expires = mbgl::Timestamp { mbgl::Seconds(
                    static_cast<mbgl::Seconds::rep>(expires / 1000)) };
            }
        }

        if (Nan::Has(res, Nan::New("etag").ToLocalChecked()).IsJust()) {
            Nan::Utf8String etag(Nan::Get(res, Nan::New("etag").ToLocalChecked()).ToLocalChecked());
            if (*etag) {
                worker->response.etag = std::string { *etag };
            }
        }

        if (Nan::Has(res, Nan::New("data").ToLocalChecked()).IsJust()) {
            auto data = Nan::Get(res, Nan::New("data").ToLocalChecked()).ToLocalChecked();
            if (node::Buffer::HasInstance(data)) {
                worker->response.data = std::make_shared<const std::string>(
                    node::Buffer::Data(data),
                    node::Buffer::Length(data)
                );
            } else {
                return Nan::ThrowTypeError("Response data must be a Buffer");
            }
        }
    }

    worker->WorkComplete();
}

void NodeRequest::HandleOKCallback() {
    // Move out of the object so callback() can only be fired once.
    auto cb = fileSourceCallback.release();

    // Send the response object to the NodeFileSource object
    (*cb)(response);

    // Clean up callback
    delete cb;
    cb = nullptr;
}

void NodeRequest::HandleErrorCallback() {
    // Move out of the object so callback() can only be fired once.
    auto cb = fileSourceCallback.release();

    response.error = std::make_unique<mbgl::Response::Error>(
        mbgl::Response::Error::Reason::Other,
        std::string{ ErrorMessage() });
  
    // Send the response object to the NodeFileSource object
    (*cb)(response);

    // Clean up callback
    delete cb;
    cb = nullptr;
}

NodeRequest::NodeAsyncRequest::NodeAsyncRequest(NodeRequest* worker_) : worker(worker_) {
    assert(worker);

    // Make sure the JS object has a pointer to this so that it can remove
    // its pointer in the destructor
    worker->asyncRequest = this;

    worker->Execute();
}

NodeRequest::NodeAsyncRequest::~NodeAsyncRequest() {
    std::cout << "~NodeAsyncRequest" << std::endl;

    if (worker) {
        // Remove the callback function because the AsyncRequest was
        // canceled and we are no longer interested in the result.
        worker->fileSourceCallback.reset();
        worker->asyncRequest = nullptr;
        worker->callback.Reset();
    }
}

Nan::Persistent<v8::Function> NodeRequest::handleCallback(Nan::New<v8::Function>(NodeRequest::HandleCallback));

}
