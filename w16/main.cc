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

class WorkerThread : public v8::internal::Thread {
  Persistent<Context> context_;
  Isolate* isolate_;
public:
  WorkerThread(const char* name, Handle<Context> context)
    : v8::internal::Thread(name) {
    isolate_ = Isolate::GetCurrent();
    context_ = Persistent<Context>::New(context);
  }

  virtual void Run() {
    SetThreadLocal(thread_name_key, const_cast<char*>(name()));

    // enter isolate
    Isolate::Scope isolate_scope(isolate_);

    // enter context
    Context::Scope context_scope(context_);

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
        HandleScope handle_scope;
        e->Execute();

        /*
        // restart transaction until it is successfully committed
        v8::internal::Transaction* transaction = NULL;
        v8::internal::Isolate* isolate =
          reinterpret_cast<v8::internal::Isolate*>(isolate_);
        while (true) {
          transaction = isolate->stm()->StartTransaction();

          HandleScope handle_scope;
          e->Execute();

          if (isolate->stm()->CommitTransaction(transaction)) {
            // printf("[%s] Comitted.\n", name());
            break;
          } else {
            // printf("[%s] Aborted.\n", name());
            v8::internal::Barrier_AtomicIncrement(
              &aborted_transactions, 1);
          }
        }
        */
        delete e;
      }
    }
  }
};

int main(int argc, char **argv) {
  // disable V8 optimisations
  char flags[1024] = { 0 };
  //strcat(flags, " --noopt");
  //strcat(flags, " --always_full_compiler");
  //strcat(flags, " --nocrankshaft");
  //strcat(flags, " --debug_code");
  //strcat(flags, " --nocompilation_cache");
  //strcat(flags, " --nouse_ic");
  V8::SetFlagsFromString(flags, strlen(flags));

  // process V8 flags and strip them from the command line
  V8::SetFlagsFromCommandLine(&argc, argv, true);

  if (argc <= 1) {
    printf("Usage: w16 <script.js> [<threads>] [<param>] [<V8 flags>]\n");
    return 1;
  }
  char* filename = argv[1];
  int threads = 1;
  int param = 1000; // just some default value

  if (argc > 2) {
    threads = atoi(argv[2]);
  }
  /*
  const int MAX_THREADS = v8::internal::CoreId::kMaxCores - 1;
  */
  const int MAX_THREADS = 1;
  if (threads < 1 || threads > MAX_THREADS) {
    printf("Threads number should be between 1 and %d.\n", MAX_THREADS);
    return 1;
  }

  if (argc > 3) {
    param = atoi(argv[3]);
  }

  V8::Initialize();

  // create a stack-allocated handle scope
  HandleScope handle_scope;

  // create a template for the global object and set built-ins
  Handle<ObjectTemplate> global = ObjectTemplate::New();
  global->Set(String::New("async"), FunctionTemplate::New(Async));
  global->Set(String::New("print"), FunctionTemplate::New(Print));
  global->Set(String::New("PARAM"), Integer::New(param));

  // create a new context
  Persistent<Context> context = Context::New(NULL, global);

  // enter the context for compiling and running the script
  Context::Scope context_scope(context);

  // load and run the script
  Script::New(ReadFile(filename), String::New(filename))->Run();

  int64_t start_time = v8::internal::OS::Ticks();

  // run event loops in worker threads
  WorkerThread* thread[MAX_THREADS];
  for (int i = 0; i < threads; i++) {
    char name[100];
    sprintf(name, "Worker %d", i);
    thread[i] = new WorkerThread(name, context);
    thread[i]->Start();
  }

  // stop when all threads are idle and the event queue is empty
  for (int i = 0; i < threads; i++) {
    thread[i]->Join();
  }

  int64_t stop_time = v8::internal::OS::Ticks();
  int milliseconds = static_cast<int>(stop_time - start_time) / 1000;
  printf("%d threads, %d param, %d ms, %d aborts\n",
    threads, param, milliseconds, aborted_transactions);

  // dispose the persistent context
  context.Dispose();

  return 0;
}
