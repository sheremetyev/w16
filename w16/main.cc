// we include internal header which includes the public one
#include <v8.h>

// standard library
#include <queue>
#include <string>
#include <fstream>

using namespace v8;

v8::internal::Thread::LocalStorageKey thread_name_key =
  v8::internal::Thread::CreateThreadLocalKey();;

// reads a file into a v8 string.
Handle<String> ReadFile(const char* filename) {
  std::ifstream in(filename, std::ios_base::in);
  std::string str;
  str.assign(std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>());

  return String::New(str.c_str());
}

// JavaScript function load(filename)
Handle<Value> Load(const Arguments& args)
{
  HandleScope handle_scope;
  String::Utf8Value filename(args[0]);
  Script::New(ReadFile(*filename), args[0])->Run();
  return Undefined();
}

// JavaScript function print(value,...)
Handle<Value> Print(const Arguments& args)
{
  static v8::internal::Mutex* print_mutex =
    v8::internal::OS::CreateMutex();
  v8::internal::ScopedLock print_lock(print_mutex);

  char* thread_name = reinterpret_cast<char*>(
    v8::internal::Thread::GetExistingThreadLocal(thread_name_key));
  HandleScope handle_scope;
  for (int i = 0; i < args.Length(); i++) {
    if (i > 0) { printf(" "); }
    String::Utf8Value str(args[i]);
    printf("[%s] %s", thread_name, static_cast<const char*>(*str));
  }
  printf("\n");
  fflush(stdout);
  return Undefined();
}

// each Event incapsulates a JavaScript closure
struct Event {
  Persistent<Function> Func;

  Event(Handle<Function> func) {
    Func = Persistent<Function>::New(func);
  }

  void Execute() {
    Func->Call(Func, 0, NULL);
  }

  ~Event() {
    Func.Dispose();
  }
};

std::queue<Event*> event_queue;
v8::internal::Atomic32 running_threads = 0;
v8::internal::Atomic32 total_transactions = 0;
v8::internal::Atomic32 aborted_transactions = 0;
v8::internal::Mutex* mutex = v8::internal::OS::CreateMutex();

// JavaScript function async(function())
Handle<Value> Async(const Arguments& args) {
  v8::internal::ScopedLock mutex_lock(mutex);

  HandleScope handle_scope;
  Handle<Function> func = Handle<Function>::Cast(args[0]);

  Event* e = new Event(func);
  event_queue.push(e);

  return Undefined();
}

void EventLoop(v8::internal::STM* stm) {
  bool active = true;
  v8::internal::Barrier_AtomicIncrement(&running_threads, 1);

  // loop until queue is empty and others are idle too
  while (true) {
    Event* e = NULL;
    {
      v8::internal::ScopedLock mutex_lock(mutex);

      if (active) {
        // count me out
        running_threads--;
        active = false;
      }

      if (!event_queue.empty()) {
        e = event_queue.front();
        event_queue.pop();
        // count me back in
        running_threads++;
        active = true;
      } else {
        if (running_threads == 0) {
          // we are done
          break;
        }
      }
    }

    if (e != NULL) {
      if (v8::internal::FLAG_stm) {
        // restart transaction until it is successfully committed
        while (true) {
          stm->StartTransaction();
          v8::internal::NoBarrier_AtomicIncrement(&total_transactions, 1);

          HandleScope handle_scope;
          e->Execute();

          if (stm->CommitTransaction()) {
            break; // while(true)
          } else {
            v8::internal::NoBarrier_AtomicIncrement(&aborted_transactions, 1);
          }
        }
      } else {
        HandleScope handle_scope;
        e->Execute();
      }
      delete e;
    }
  }
}

class WorkerThread : public v8::internal::Thread {
  Persistent<Context> context_;
  Isolate* isolate_;
  v8::internal::STM* stm_;
public:
  WorkerThread(const char* name, Handle<Context> context,
               v8::internal::STM* stm)
    : v8::internal::Thread(name), stm_(stm) {
    isolate_ = Isolate::GetCurrent();
    context_ = Persistent<Context>::New(context);
  }

  virtual void Run() {
    SetThreadLocal(thread_name_key, const_cast<char*>(name()));

    // enter isolate
    Isolate::Scope isolate_scope(isolate_);

    // enter context
    Context::Scope context_scope(context_);

    // run event loop
    EventLoop(stm_);
  }
};

int main(int argc, char **argv) {
  // disable V8 optimisations
  char flags[1024] = { 0 };
  strcat(flags, " --nostm");
  strcat(flags, " --nouse-ic"); // disable inline caching
  strcat(flags, " --noinline-new"); // disable inline allocation
  V8::SetFlagsFromString(flags, strlen(flags));

  // process V8 flags and strip them from the command line
  V8::SetFlagsFromCommandLine(&argc, argv, true);

  if (argc <= 1) {
    printf("Usage: w16 <script.js> [--threads=<n>] [<V8 flags>]\n");
    return 1;
  }
  char* filename = argv[1];
  int threads = v8::internal::FLAG_threads;

  /*
  const int MAX_THREADS = v8::internal::CoreId::kMaxCores - 1;
  */
  const int MAX_THREADS = 1;
  if (threads < 1 || threads > MAX_THREADS) {
    printf("Threads number should be between 1 and %d.\n", MAX_THREADS);
    return 1;
  }

  if (!v8::internal::FLAG_stm && v8::internal::FLAG_threads > 1) {
    printf("Threads number should be 1 in non-transactional mode.\n");
    return 1;
  }

  Isolate::Scope isolate_scope(Isolate::GetCurrent());

  V8::Initialize();

  // create a stack-allocated handle scope
  HandleScope handle_scope;

  // create a template for the global object and set built-ins
  Handle<ObjectTemplate> global = ObjectTemplate::New();
  global->Set(String::New("load"),  FunctionTemplate::New(Load));
  global->Set(String::New("async"), FunctionTemplate::New(Async));
  global->Set(String::New("print"), FunctionTemplate::New(Print));

  // create a new context
  Persistent<Context> context = Context::New(NULL, global);

  // enter the context for compiling and running the script
  Context::Scope context_scope(context);

  Isolate* isolate = Isolate::GetCurrent();
  v8::internal::STM* stm =
    reinterpret_cast<v8::internal::Isolate*>(isolate)->stm();

  int64_t start_time = v8::internal::OS::Ticks();

  // load and run the initial script in a transaction
  if (v8::internal::FLAG_stm) {
    stm->StartTransaction();
    Script::New(ReadFile(filename), String::New(filename))->Run();
    ASSERT(stm->CommitTransaction());
  } else {
    Script::New(ReadFile(filename), String::New(filename))->Run();
  }

  // run event loops in worker threads (less the loop running in main thread)
  WorkerThread* thread[MAX_THREADS];
  for (int i = 0; i < threads-1; i++) {
    char name[100];
    sprintf(name, "Worker %d", i+1);
    thread[i] = new WorkerThread(name, context, stm);
    thread[i]->Start();
  }

  // run event loop in main thread too
  v8::internal::Thread::SetThreadLocal(thread_name_key, "Worker 0");
  EventLoop(stm);

  // stop when all threads are idle and the event queue is empty
  for (int i = 0; i < threads-1; i++) {
    thread[i]->Join();
  }

  int64_t stop_time = v8::internal::OS::Ticks();
  int milliseconds = static_cast<int>(stop_time - start_time) / 1000;
  printf("%d threads, %d ms, %d transactions, %d aborts\n",
    threads, milliseconds, total_transactions, aborted_transactions);

  // dispose the persistent context
  context.Dispose();

  return 0;
}
