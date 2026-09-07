#include <napi.h>
Napi::String Version(const Napi::CallbackInfo& info){ return Napi::String::New(info.Env(),"0.1.1"); }
Napi::Object Init(Napi::Env env, Napi::Object exports){ exports.Set("version", Napi::Function::New(env, Version)); return exports; }
NODE_API_MODULE(fastgit, Init)
