// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "node_script.h"
#include "node_watchdog.h"
#include <assert.h>

namespace node {
    
    using v8::Array;
    using v8::Context;
    using v8::Exception;
    using v8::Function;
    using v8::FunctionCallbackInfo;
    using v8::FunctionTemplate;
    using v8::Handle;
    using v8::HandleScope;
    using v8::Local;
    using v8::Object;
    using v8::Persistent;
    using v8::Script;
    using v8::String;
    using v8::TryCatch;
    using v8::V8;
    using v8::Value;
    
    
    class WrappedContext : ObjectWrap {
    public:
        static void Initialize(Handle<Object> target);
        static void New(const FunctionCallbackInfo<Value>& args);
        
        Local<Context> GetV8Context();
        static Local<Object> NewInstance();
        static bool InstanceOf(Handle<Value> value);
        
    protected:
        static Persistent<FunctionTemplate> constructor_template;// 只能保存一个 template? 保存的是当前对象 constructor function
        
        WrappedContext();
        ~WrappedContext();
        
        Persistent<Context> context_;
    };
    
    
    Persistent<FunctionTemplate> WrappedContext::constructor_template;
    
    
    class WrappedScript : ObjectWrap {
    public:
        static void Initialize(Handle<Object> target);
        
        enum EvalInputFlags { compileCode, unwrapExternal };// 需要做什么事情
        enum EvalContextFlags { thisContext, newContext, userContext };// 执行的 context
        enum EvalOutputFlags { returnResult, wrapExternal };// 返回方式
        enum EvalTimeoutFlags { noTimeout, useTimeout };
        
        template <EvalInputFlags input_flag, EvalContextFlags context_flag, EvalOutputFlags output_flag, EvalTimeoutFlags timeout_flag>
        static void EvalMachine(const FunctionCallbackInfo<Value>& args);
        
    protected:
        WrappedScript() : ObjectWrap() {}
        ~WrappedScript();
        
        static void New(const FunctionCallbackInfo<Value>& args);
        static void CreateContext(const FunctionCallbackInfo<Value>& args);
        
        static void RunInContext(const FunctionCallbackInfo<Value>& args);
        static void RunInThisContext(const FunctionCallbackInfo<Value>& args);
        static void RunInNewContext(const FunctionCallbackInfo<Value>& args);
        
        static void CompileRunInContext(const FunctionCallbackInfo<Value>& args);
        static void CompileRunInThisContext(const FunctionCallbackInfo<Value>& args);
        static void CompileRunInNewContext(const FunctionCallbackInfo<Value>& args);
        
        Persistent<Script> script_;// 一个编译好的文件?
    };
    
    /**
     * 复制对象的所有属性
     */
    void CloneObject(Handle<Object> recv,
                     Handle<Value> source,
                     Handle<Value> target) {
        HandleScope scope(node_isolate);
        
        const char raw_script_source[] =
        "(function(source, target) {                                    \n"
        "  Object.getOwnPropertyNames(source).forEach(function(key) {   \n"
        "    try {                                                      \n"
        "      var desc = Object.getOwnPropertyDescriptor(source, key); \n"
        "      if (desc.value === source) desc.value = target;          \n"
        "      Object.defineProperty(target, key, desc);                \n"
        "    } catch (e) {                                              \n"
        "     // Catch sealed properties errors                         \n"
        "    }                                                          \n"
        "  });                                                          \n"
        "});                                                            \n";
        
        Local<String> script_source = String::New(raw_script_source, sizeof(raw_script_source) - 1);
        Local<Script> script = Script::Compile(script_source, String::New("binding:script"));
        
        Local<Function> fun = script->Run().As<Function>();
        assert(fun.IsEmpty() == false);
        assert(fun->IsFunction() == true);
        
        Handle<Value> argv[] = { source, target };
        Handle<Value> rc = fun->Call(recv, ARRAY_SIZE(argv), argv);
        assert(rc.IsEmpty() == false);
    }
    
    /**
     * 初始化构造方法并且绑定到 target.Context 对象上
     */
    void WrappedContext::Initialize(Handle<Object> target) {
        HandleScope scope(node_isolate);
        
        Local<FunctionTemplate> t = FunctionTemplate::New(WrappedContext::New);
        t->InstanceTemplate()->SetInternalFieldCount(1);// <--- 未来 Context 实例绑定的位置
        t->SetClassName(String::NewSymbol("Context"));
        
        target->Set(String::NewSymbol("Context"), t->GetFunction());
        constructor_template.Reset(node_isolate, t);// 这个方法应该只被调用一次
    }
    
    /**
     * 判断给定对象是否是 Context 的实例
     */
    bool WrappedContext::InstanceOf(Handle<Value> value) {
        return !value.IsEmpty() && HasInstance(constructor_template, value);
    }
    
    /**
     * 建立一个 WrappedContext 的实例并且把它绑定到对应的实例对象上
     */
    void WrappedContext::New(const FunctionCallbackInfo<Value>& args) {
        HandleScope scope(node_isolate);
        WrappedContext *t = new WrappedContext();
        t->Wrap(args.This());
    }
    
    /**
     * 每当 new 被调用的时候就使用全局的 node_isolate 来建立一个新的 context
     */
    WrappedContext::WrappedContext() : ObjectWrap() {
        context_.Reset(node_isolate, Context::New(node_isolate));
    }
    
    
    WrappedContext::~WrappedContext() {
        context_.Dispose();
    }
    
    /**
     * 建立一个对象, 内含一个 v8::Context 对象的实例
     */
    Local<Object> WrappedContext::NewInstance() {
        Local<FunctionTemplate> constructor_template_handle = PersistentToLocal(constructor_template);
        return constructor_template_handle->GetFunction()->NewInstance();
    }
    
    /**
     * 获得内部的 Context
     */
    Local<Context> WrappedContext::GetV8Context() {
        return Local<Context>::New(node_isolate, context_);
    }
    
    /**
     * 在 tarage 中增加 NodeScript class
     */
    void WrappedScript::Initialize(Handle<Object> target) {
        HandleScope scope(node_isolate);
        
        Local<FunctionTemplate> t = FunctionTemplate::New(WrappedScript::New);
        t->InstanceTemplate()->SetInternalFieldCount(1);// 为了绑定 this 对象
        // Note: We use 'NodeScript' instead of 'Script' so that we do not
        // conflict with V8's Script class defined in v8/src/messages.js
        // See GH-203 https://github.com/joyent/node/issues/203
        t->SetClassName(String::NewSymbol("NodeScript"));
        
        // 完全重复, 这是闹哪样
        NODE_SET_PROTOTYPE_METHOD(t, "createContext", WrappedScript::CreateContext);
        NODE_SET_PROTOTYPE_METHOD(t, "runInContext", WrappedScript::RunInContext);
        NODE_SET_PROTOTYPE_METHOD(t, "runInThisContext", WrappedScript::RunInThisContext);
        NODE_SET_PROTOTYPE_METHOD(t, "runInNewContext", WrappedScript::RunInNewContext);
        NODE_SET_METHOD(t, "createContext", WrappedScript::CreateContext);
        NODE_SET_METHOD(t, "runInContext", WrappedScript::CompileRunInContext);
        NODE_SET_METHOD(t, "runInThisContext", WrappedScript::CompileRunInThisContext);
        NODE_SET_METHOD(t, "runInNewContext", WrappedScript::CompileRunInNewContext);
        
        target->Set(String::NewSymbol("NodeScript"), t->GetFunction());
    }
    
    
    void WrappedScript::New(const FunctionCallbackInfo<Value>& args) {
        assert(args.IsConstructCall() == true);

        HandleScope scope(node_isolate);
        WrappedScript *t = new WrappedScript();
        t->Wrap(args.This());// 建立一个 t 对象, 并且绑定到 args.this

        WrappedScript::EvalMachine<compileCode, thisContext, wrapExternal, noTimeout>(args);
    }
    
    
    WrappedScript::~WrappedScript() {
        script_.Dispose();
    }
    
    void WrappedScript::CreateContext(const FunctionCallbackInfo<Value>& args) {
        HandleScope scope(node_isolate);
        
        Local<Object> context = WrappedContext::NewInstance();
        
        if (args.Length() > 0) {
            if (args[0]->IsObject()) {
                Local<Object> sandbox = args[0].As<Object>();
                
                CloneObject(args.This(), sandbox, context);
            } else {
                return ThrowTypeError("createContext() accept only object as first argument.");
            }
        }
        
        args.GetReturnValue().Set(context);
    }
    
    
    void WrappedScript::RunInContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<
        unwrapExternal, userContext, returnResult, useTimeout>(args);
    }
    
    
    void WrappedScript::RunInThisContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<unwrapExternal, thisContext, returnResult, useTimeout>(args);
    }
    
    void WrappedScript::RunInNewContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<unwrapExternal, newContext, returnResult, useTimeout>(args);
    }
    
    
    void WrappedScript::CompileRunInContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<compileCode, userContext, returnResult, useTimeout>(args);
    }
    
    
    void WrappedScript::CompileRunInThisContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<compileCode, thisContext, returnResult, useTimeout>(args);
    }
    
    
    void WrappedScript::CompileRunInNewContext(const FunctionCallbackInfo<Value>& args) {
        WrappedScript::EvalMachine<compileCode, newContext, returnResult, useTimeout>(args);
    }
    
    
    template <WrappedScript::EvalInputFlags input_flag,
    WrappedScript::EvalContextFlags context_flag,
    WrappedScript::EvalOutputFlags output_flag,
    WrappedScript::EvalTimeoutFlags timeout_flag>
    void WrappedScript::EvalMachine(const FunctionCallbackInfo<Value>& args) {
        HandleScope scope(node_isolate);
        
        // 如果输入是 code, 那么 args[0] 就应该是 code
        if (input_flag == compileCode && args.Length() < 1) {
            return ThrowTypeError("needs at least 'code' argument.");
        }

        // 如果是使用给定的 context, 那么 code 之后的 args 就应该是给定的 context
        const int sandbox_index = input_flag == compileCode ? 1 : 0;
        if (context_flag == userContext && !WrappedContext::InstanceOf(args[sandbox_index])) {
            return ThrowTypeError("needs a 'context' argument.");
        }
        
        // 如果是 code, 取得这个 code
        Local<String> code;
        if (input_flag == compileCode) {
            code = args[0]->ToString();
        }
        
        // 建立执行的 context, 或者使用给定的 context
        Local<Object> sandbox;// 默认就是建立一个当然的 context
        if (context_flag == newContext) {
            sandbox = args[sandbox_index]->IsObject() ? args[sandbox_index]->ToObject() : Object::New();
        } else if (context_flag == userContext) {
            sandbox = args[sandbox_index]->ToObject();
        }
        
        // context 之后就是 file 那么
        const int filename_index = sandbox_index + (context_flag == thisContext? 0 : 1);// 如果传递了 context, 那么name就是下个值, 否则那么就是现在的值
        Local<String> filename = args.Length() > filename_index ? args[filename_index]->ToString() : String::New("evalmachine.<anonymous>");
        
        // 超时
        uint64_t timeout = 0;
        const int timeout_index = filename_index + 1;
        if (timeout_flag == useTimeout && args.Length() > timeout_index) {
            if (!args[timeout_index]->IsUint32()) {
                return ThrowTypeError("needs an unsigned integer 'ms' argument.");
            }
            timeout = args[timeout_index]->Uint32Value();
        }
        
        // 是否显示错误
        const int display_error_index = timeout_index + (timeout_flag == noTimeout ? 0 : 1);
        bool display_error = false;
        if (args.Length() > display_error_index && args[display_error_index]->IsBoolean() && args[display_error_index]->BooleanValue() == true) {
            display_error = true;
        }
        
        // 确定实际使用的 context
        Local<Context> context = Context::GetCurrent();
        Local<Array> keys;
        if (context_flag == newContext) {
            // Create the new context
            context = Context::New(node_isolate);
            
        } else if (context_flag == userContext) {
            // Use the passed in context
            WrappedContext *nContext = ObjectWrap::Unwrap<WrappedContext>(sandbox);
            context = nContext->GetV8Context();
        }
        
        // 这样就能进入 context 了
        Context::Scope context_scope(context);
        
        // New and user context share code. DRY it up.
        if (context_flag == userContext || context_flag == newContext) {
            // Copy everything from the passed in sandbox (either the persistent context for runInContext(), or the sandbox arg to runInNewContext()).
            CloneObject(args.This(), sandbox, context->Global()->GetPrototype());
        }
        
        // Catch errors
        TryCatch try_catch;
        
        // TryCatch must not be verbose to prevent duplicate logging
        // of uncaught exceptions (we are rethrowing them)
        try_catch.SetVerbose(false);
        
        Handle<Value> result;
        Local<Script> script;
        
        // 
        if (input_flag == compileCode) {
            // well, here WrappedScript::New would suffice in all cases, but maybe
            // Compile has a little better performance where possible
            script = output_flag == returnResult ? Script::Compile(code, filename) : Script::New(code, filename);
            if (script.IsEmpty()) {
                // FIXME UGLY HACK TO DISPLAY SYNTAX ERRORS.
                if (display_error) {
                    DisplayExceptionLine(try_catch.Message());  
                }
                
                // Hack because I can't get a proper stacktrace on SyntaxError
                try_catch.ReThrow();
                return;
            }
        } else {
            // 当前对象已经是一个包装过的 script 对象了
            WrappedScript *n_script = ObjectWrap::Unwrap<WrappedScript>(args.This());
            if (!n_script) {
                return ThrowError("Must be called as a method of Script.");
            } else if (n_script->script_.IsEmpty()) {
                return ThrowError("'this' must be a result of previous new Script(code) call.");
            }
            script = Local<Script>::New(node_isolate, n_script->script_);
        }
        
        // run and return result
        if (output_flag == returnResult) {
            if (timeout) {
                Watchdog wd(timeout);
                result = script->Run();
            } else {
                result = script->Run();
            }
            if (try_catch.HasCaught() && try_catch.HasTerminated()) {
                V8::CancelTerminateExecution(args.GetIsolate());
                return ThrowError("Script execution timed out.");
            }
            if (result.IsEmpty()) {
                if (display_error) DisplayExceptionLine(try_catch.Message());
                try_catch.ReThrow();
                return;
            }
        } else {
            // wrapExternal, 好像替换了 Script 对象?
            WrappedScript *n_script = ObjectWrap::Unwrap<WrappedScript>(args.This());
            if (!n_script) {
                return ThrowError("Must be called as a method of Script.");
            }
            n_script->script_.Reset(node_isolate, script);
            result = args.This();
        }
        
        // 这有点太傻了吧
        if (context_flag == userContext || context_flag == newContext) {
            // success! copy changes back onto the sandbox object.
            CloneObject(args.This(), context->Global()->GetPrototype(), sandbox);
        }
        
        args.GetReturnValue().Set(result);
    }
    
    
    void InitEvals(Handle<Object> target) {
        HandleScope scope(node_isolate);
        WrappedContext::Initialize(target);
        WrappedScript::Initialize(target);
    }
    
    
}  // namespace node

NODE_MODULE(node_evals, node::InitEvals)
