// v8 includes
#include <v8.h> // we include internal header which includes public one

// standard library
#include <queue>
#include <string>
#include <fstream>

using namespace v8;

v8::internal::Thread::LocalStorageKey thread_name_key = 
    v8::internal::Thread::CreateThreadLocalKey();;

// Reads a file into a v8 string.
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
    static v8::internal::Mutex* print_mutex = v8::internal::OS::CreateMutex();
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

// Each event incapsulates a JavaScript closure
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

// Event queue
std::queue<Event*> events;
int running_threads = 0;
v8::internal::Mutex* mutex = v8::internal::OS::CreateMutex();

// JavaScript function enqueue(function())
Handle<Value> Enqueue(const Arguments& args) {
    v8::internal::ScopedLock mutex_lock(mutex);

    HandleScope handle_scope;
    Handle<Function> func = Handle<Function>::Cast(args[0]);
    
    Event* e = new Event(func);
    events.push(e);
    
    return Undefined();
}

class WorkerThread : public v8::internal::Thread {
    Persistent<Context> context_;
public:
    WorkerThread(const char* name, Handle<Context> context) : 
      v8::internal::Thread(reinterpret_cast<v8::internal::Isolate*>(
          Isolate::GetCurrent()), name) {
        context_ = Persistent<Context>::New(context);
    }

    virtual void Run() {
        SetThreadLocal(thread_name_key, const_cast<char*>(name()));

        Isolate::Scope isolate_scope(reinterpret_cast<Isolate*>(isolate()));

        // Create a new context and enter it
        Context::Scope cscope(context_);

        bool active = true;
        {
            v8::internal::ScopedLock mutex_lock(mutex);
            running_threads++;
        }

        // Loop until queue is empty and others are idle too
        while (true) {
            Event* e = NULL;
            {
                v8::internal::ScopedLock mutex_lock(mutex);

                if (active) {
                    // count me out
                    running_threads--;
                    active = false;
                }

                if (!events.empty()) {
                    e = events.front();
                    events.pop();
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
                // repeat attempts until success
                v8::internal::Transaction* transaction = NULL;
                while (true) {
                    transaction = isolate()->stm()->StartTransaction();
                    
                    HandleScope handle_scope;
                    e->Execute();
                    
                    if (isolate()->stm()->CommitTransaction(transaction)) {
                        // printf("[%s] Comitted.\n", name());
                        break;
                    } else {
                        // printf("[%s] Aborted.\n", name());
                    }
                }
                delete e;
            }
        }
    }
};

int main(int argc, char **argv)
{
    if (argc <= 1) {
        printf("Usage: w16 script.js\n");
        return 1;
    }
    char* filename = argv[1];

    // Disable optimizing profiler to get rid of an extra thread
    char flags[1024] = { 0 };
    strcat(flags, " --noopt");
    strcat(flags, " --always_full_compiler");
    strcat(flags, " --nocrankshaft");
    strcat(flags, " --debug_code");
    strcat(flags, " --nocompilation_cache");
    strcat(flags, " --nouse_ic");
    V8::SetFlagsFromString(flags, strlen(flags));
    int argcc = argc - 1;
    V8::SetFlagsFromCommandLine(&argcc, argv+1, false);

    V8::Initialize();

    // Create a stack-allocated handle scope
    HandleScope handle_scope;

    // Create a template for the global object and set built-in functions
    Handle<ObjectTemplate> global = ObjectTemplate::New();
    global->Set(String::New("enqueue"), FunctionTemplate::New(Enqueue));
    global->Set(String::New("print"), FunctionTemplate::New(Print));

    // Create a new context and enter it
    Persistent<Context> context = Context::New(NULL, global);

    // Enter the context for compiling and running the script
    Context::Scope cscope(context);

    // Load and run the script
    Script::New(ReadFile(filename), String::New(filename))->Run();

    int64_t start_time = v8::internal::OS::Ticks();

    // Run event loops in worker threads
    const int THREADS_COUNT = 1;
    WorkerThread* thread[THREADS_COUNT];
    for (int i = 0; i < THREADS_COUNT; i++) {
        char name[100];
        sprintf(name, "Worker %d", i);
        thread[i] = new WorkerThread(name, context);
        thread[i]->Start();
    }

    // We stop when all threads are idle and the event queue is empty
    for (int i = 0; i < THREADS_COUNT; i++) {
        thread[i]->Join();
    }

    int64_t stop_time = v8::internal::OS::Ticks();
    int milliseconds = static_cast<int>(stop_time - start_time) / 1000;
    printf("%d milliseconds elapsed.\n", milliseconds);

    // Dispose the persistent context
    context.Dispose();

    return 0;
}
