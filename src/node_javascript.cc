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
#include "node_natives.h"// 预加载的 js 模块
#include "v8.h"

#include <string.h>
#if !defined(_MSC_VER)
#include <strings.h>
#endif

namespace node {
using v8::Handle;
using v8::HandleScope;
using v8::Local;
using v8::Object;
using v8::String;

/**
 * node_native.js
 */
Handle<String> MainSource() {
    return String::New(node_native, sizeof(node_native) - 1);
}

/**
 * 把所有native模块加载到 target
 */
void DefineJavaScript(Handle<Object> target) {
    HandleScope scope(node_isolate);
    
    for (int i = 0; natives[i].name; i++) {
        if (natives[i].source != node_native) {
            Local<String> name = String::New(natives[i].name);
            Handle<String> source = String::New(natives[i].source, natives[i].source_len);
            target->Set(name, source);
        }
    }
}
}  // namespace node
