#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wshadow"
#include <nan.h>
#pragma GCC diagnostic pop

#include "node_map.hpp"

#include <mbgl/storage/file_source.hpp>

namespace node_mbgl {

class NodeRequest : public Nan::AsyncWorker, public Nan::ObjectWrap {
public:
    static NAN_MODULE_INIT(Init);
    static Nan::Persistent<v8::Function> constructor;
    static void New(const Nan::FunctionCallbackInfo<v8::Value>&);

    NodeRequest(NodeMap*, mbgl::FileSource::Callback);
    virtual ~NodeRequest();

    virtual void Execute();
    virtual void WorkComplete();

    static void HandleCallback(const Nan::FunctionCallbackInfo<v8::Value>& info);

    struct NodeAsyncRequest : public mbgl::AsyncRequest {
        NodeAsyncRequest(NodeRequest*);
        ~NodeAsyncRequest() override;

        NodeRequest* request;
    };

    Nan::Persistent<v8::Function> callbackFn;

private:
    NodeMap* target;
    mbgl::FileSource::Callback callback;
    NodeAsyncRequest* asyncRequest = nullptr;

    mbgl::Response response;

    static Nan::Persistent<v8::FunctionTemplate> callbackTemplate;
};

}
