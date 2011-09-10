#include "v8.h"
#include "stm.h"
#include "isolate.h"

#include <set>
#include <map>

namespace v8 {
namespace internal {

CoreId CoreId::Current() {
  int thread_id = ThreadId::Current().ToInteger();
  ASSERT(thread_id >= 0);
  ASSERT(thread_id <= kMaxCores);
  return CoreId(thread_id - 1);
}

class Transaction {
  typedef std::set<JSObject*> ObjectSet;
  typedef std::map<JSObject*, JSObject*> ObjectMap;

public:
  Transaction(Isolate* isolate) :
      aborted_(false),
      isolate_(isolate),
      mutex_(OS::CreateMutex()) {
  }

  JSObject* RedirectRead(JSObject* obj) {
    ASSERT_NOT_NULL(obj);

    // if aborted return NULL to terminate
    if (aborted_) {
      return NULL;
    }

    // lookup in write set and redirect if included
    ObjectMap::const_iterator it = write_set_.find(obj);
    if (it != write_set_.end()) {
      return it->second;
    }

    // lookup in read set and return if included
    if (read_set_.find(obj) != read_set_.end()) {
      return obj;
    }

    // include in read set and return
    ScopedLock lock(mutex_);
    read_set_.insert(obj);
    return obj;
  }

  JSObject* RedirectWrite(JSObject* obj) {
    ASSERT_NOT_NULL(obj);

    // if aborted return NULL to terminate
    if (aborted_) {
      return NULL;
    }

    // lookup in copy set and return if included
    if (copy_set_.find(obj) != copy_set_.end()) {
      return obj;
    }

    // lookup in write set and return if included
    ObjectMap::const_iterator it = write_set_.find(obj);
    if (it != write_set_.end()) {
      return it->second;
    }

    // make a copy, include it in write set and return
    Object* result;
    { MaybeObject* maybe_result = isolate_->heap()->CopyJSObject(obj);
      if (!maybe_result->ToObject(&result)) return NULL;
    }
    JSObject* copy = JSObject::cast(result);
    copy_set_.insert(copy);

    ScopedLock lock(mutex_);
    write_set_.insert(ObjectMap::value_type(obj, copy));

    return copy;
  }

  void CommitHeap() {
    // copy all objects in write set back to their original location
    Heap* heap = isolate_->heap();
    for (ObjectMap::const_iterator it = write_set_.begin();
         it != write_set_.end(); ++it) {
      Address dst_addr = it->first->address();
      Address src_addr = it->second->address();
      heap->CopyBlock(dst_addr, src_addr, it->first->Size());
    }
  }

  bool HasConflicts(Transaction* other) {
    const ObjectMap& other_write_set_ = other->write_set_;
    for (ObjectMap::const_iterator it = other_write_set_.begin();
         it != other_write_set_.end(); ++it) {
      JSObject* obj = it->first;
      if (read_set_.find(obj) != read_set_.end()) {
        //printf("RW conflict on "); obj->PrintLn();
        return true;
      }
      if (write_set_.find(obj) != write_set_.end()) {
        //printf("WW conflict on "); obj->PrintLn();
        return true;
      }
    }
    return false;
  }

  void Lock() { mutex_->Lock(); }
  void Unlock() { mutex_->Unlock(); }
  
  void Abort() { aborted_ = true; }
  bool IsAborted() { return aborted_; }

  void ClearExceptions() {
    isolate_->clear_pending_exception();
    isolate_->clear_pending_message();
  }

private:
  volatile bool aborted_;
  Isolate* isolate_;
  ObjectSet read_set_;
  ObjectMap write_set_;
  ObjectSet copy_set_; // temporary copies we created
  Mutex* mutex_;
};

Transaction* STM::StartTransaction() {
  Transaction* trans = new Transaction(isolate_);
  isolate_->set_transaction(trans);

  ScopedLock transactions_lock(transactions_mutex_);
  transactions_.Add(trans);
  return trans;
}

STM::STM() {
  transactions_mutex_ = OS::CreateMutex();
  commit_mutex_ = OS::CreateMutex();
}

bool STM::CommitTransaction(Transaction* trans) {
  ASSERT_NOT_NULL(trans);
  ASSERT_EQ(trans, isolate_->get_transaction());

  ScopedLock commit_lock(commit_mutex_);
  ScopedLock transactions_lock(transactions_mutex_);

  bool comitted = false;

  // if the transaction was aborted then clear exceptions and return false
  if (trans->IsAborted()) {
    trans->ClearExceptions();
  } else {
    // lock all transactions
    for (int i = 0; i < transactions_.length(); i++) {
      transactions_[i]->Lock();
    }

    // intersect write set with other transactions and abort those in conflict
    for (int i = 0; i < transactions_.length(); i++) {
      Transaction* t = transactions_[i];
      if (t == trans) {
        continue;
      }
      if (t->HasConflicts(trans)) {
        t->Abort();
      }
    }

    // copy write set back to the heap
    trans->CommitHeap();

    // unlock all transactions
    for (int i = 0; i < transactions_.length(); i++) {
      transactions_[i]->Unlock();
    }

    comitted = true;
  }

  isolate_->set_transaction(NULL);

  ASSERT(transactions_.RemoveElement(trans));
  delete trans;
  return comitted;
}

JSObject* STM::RedirectRead(JSObject* obj) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectRead(obj);
}

JSObject* STM::RedirectWrite(JSObject* obj) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectWrite(obj);
}

} }  // namespace v8::internal
