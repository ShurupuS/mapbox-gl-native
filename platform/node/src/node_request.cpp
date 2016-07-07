#include "node_request.hpp"
#include <mbgl/storage/response.hpp>
#include <mbgl/util/chrono.hpp>

#include <cmath>
#include <iostream>

namespace node_mbgl {

NodeRequest::NodeRequest(
    NodeMap* target_,
    mbgl::FileSource::Callback callback_)
    : AsyncWorker(nullptr),
    target(target_),
    callback(callback_) {
    auto fn = Nan::New(callbackTemplate)->GetFunction();

    // Bind a reference to this object on the callback function
    fn->SetHiddenValue(Nan::New("worker").ToLocalChecked(), Nan::New<v8::External>(this));

    callbackFn.Reset(fn);
}

NodeRequest::~NodeRequest() {
    std::cout << "~NodeRequest" << std::endl;

    // When this object gets destroyed, make sure that the
    // AsyncRequest can no longer attempt to remove the callback function
    // this object was holding (it can't be fired anymore).
    if (asyncRequest) {
        asyncRequest->request = nullptr;
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
        Nan::New(callbackTemplate)->GetFunction()
    };

    Nan::MakeCallback(Nan::To<v8::Object>(target->handle()->GetInternalField(1)).ToLocalChecked(), "request", 2, argv);
}

void NodeRequest::WorkComplete() {
    // no-op
}

void NodeRequest::HandleCallback(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto hiddenValue = info.Callee()->GetHiddenValue(Nan::New("worker").ToLocalChecked());
    auto external = hiddenValue.As<v8::External>();
    auto externalValue = external->Value();
    auto request = reinterpret_cast<NodeRequest*>(externalValue);

    std::cout << "HandleCallback " << !request->callback << std::endl;

    // Move out of the object so callback() can only be fired once.
    auto callback = std::move(request->callback);
    if (!callback) {
        info.GetReturnValue().SetUndefined();
        return;
    }

    mbgl::Response response;

    if (info[0]->IsObject()) {
        auto err = info[0]->ToObject();
        auto msg = Nan::New("message").ToLocalChecked();

        if (Nan::Has(err, msg).IsJust()) {
            request->SetErrorMessage(*Nan::Utf8String(
                Nan::Get(err, msg).ToLocalChecked()));
        }
    } else if (info[0]->IsString()) {
        request->SetErrorMessage(*Nan::Utf8String(info[0]));
    } else if (info.Length() < 1) {
        response.noContent = true;
    } else if (info.Length() < 2 || !info[1]->IsObject()) {
        return Nan::ThrowTypeError("Second argument must be a response object");
    } else {
        auto res = info[1]->ToObject();

        if (Nan::Has(res, Nan::New("modified").ToLocalChecked()).IsJust()) {
            const double modified = Nan::To<double>(Nan::Get(res, Nan::New("modified").ToLocalChecked()).ToLocalChecked()).FromJust();
            if (!std::isnan(modified)) {
                response.modified = mbgl::Timestamp { mbgl::Seconds(
                    static_cast<mbgl::Seconds::rep>(modified / 1000)) };
            }
        }

        if (Nan::Has(res, Nan::New("expires").ToLocalChecked()).IsJust()) {
            const double expires = Nan::To<double>(Nan::Get(res, Nan::New("expires").ToLocalChecked()).ToLocalChecked()).FromJust();
            if (!std::isnan(expires)) {
                response.expires = mbgl::Timestamp { mbgl::Seconds(
                    static_cast<mbgl::Seconds::rep>(expires / 1000)) };
            }
        }

        if (Nan::Has(res, Nan::New("etag").ToLocalChecked()).IsJust()) {
            Nan::Utf8String etag(Nan::Get(res, Nan::New("etag").ToLocalChecked()).ToLocalChecked());
            if (*etag) {
                response.etag = std::string { *etag };
            }
        }

        if (Nan::Has(res, Nan::New("data").ToLocalChecked()).IsJust()) {
            auto data = Nan::Get(res, Nan::New("data").ToLocalChecked()).ToLocalChecked();
            if (node::Buffer::HasInstance(data)) {
                response.data = std::make_shared<const std::string>(
                    node::Buffer::Data(data),
                    node::Buffer::Length(data)
                );
            } else {
                return Nan::ThrowTypeError("Response data must be a Buffer");
            }
        }
    }

    if (request->ErrorMessage()) {
        std::cout << "Callback with error" << std::endl;
        response.error = std::make_unique<mbgl::Response::Error>(
            mbgl::Response::Error::Reason::Other,
            std::string{ request->ErrorMessage() });
    } else {
        std::cout << "Callback with response" << std::endl;
    }

    // Send the response object to the NodeFileSource object
    callback(response);
    info.GetReturnValue().SetUndefined();
}

NodeRequest::NodeAsyncRequest::NodeAsyncRequest(NodeRequest* request_) : request(request_) {
    assert(request);

    // Make sure the JS object has a pointer to this so that it can remove
    // its pointer in the destructor
    request->asyncRequest = this;
}

NodeRequest::NodeAsyncRequest::~NodeAsyncRequest() {
    if (request) {
        // Remove the callback function because the AsyncRequest was
        // canceled and we are no longer interested in the result.
        // request->fileSourceCallback.reset();
        request->callback = {};
        request->asyncRequest = nullptr;
    }
}

Nan::Persistent<v8::FunctionTemplate> NodeRequest::callbackTemplate(Nan::New<v8::FunctionTemplate>(NodeRequest::HandleCallback));

}
