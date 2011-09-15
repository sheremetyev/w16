#include "v8.h"
#include "stm.h"
#include "isolate.h"

#include <set>
#include <map>

namespace v8 {
namespace internal {

class WriteSet {
 public:
  void Iterate(ObjectVisitor* v) {
  }

  Handle<Object> Get(Handle<Object> obj) {
    return obj;
  }

  Handle<Object> Add(Handle<Object> obj, Object* redirect) {
    return Handle<Object>::null();
  }

  void CommitChanges(Heap* heap) {
    // heap->CopyBlock(dst_addr, src_addr, dst->Size());
  }

  bool Intersects(const WriteSet& other) {
    return false;
  }
  
 private:

   friend class ReadSet;
};

class ReadSet {
 public:
  void Iterate(ObjectVisitor* v) {
  }

  Handle<Object> Get(Handle<Object> obj) {
    return obj;
  }

  Handle<Object> Add(Handle<Object> obj) {
    return Handle<Object>::null();
  }

  bool Intersects(const WriteSet& other) {
    return false;
  }
};

class Transaction {
 public:
  Transaction(Isolate* isolate) :
    aborted_(false),
    isolate_(isolate),
    mutex_(OS::CreateMutex()) {
  }

  void Iterate(ObjectVisitor* v) {
    read_set_.Iterate(v);
    write_set_.Iterate(v);
  }

  Handle<Object> RedirectLoad(Handle<Object> obj) {
    ASSERT(!obj.is_null());

    if (!obj->IsJSObject()) {
      return obj;
    }

    // if aborted return null handle to terminate current transaction
    if (aborted_) {
      return Handle<Object>::null();
    }

    // lookup in write set and redirect if included
    Handle<Object> redirect = write_set_.Get(obj);
    if (!redirect.is_null())
      return redirect;

    // lookup in read set and return if included
    redirect = read_set_.Get(obj);
    if (!redirect.is_null())
      return redirect;

    // include in read set and return
    ScopedLock lock(mutex_);
    return read_set_.Add(obj);
  }

  Handle<Object> RedirectStore(Handle<Object> obj) {
    ASSERT(!obj.is_null());

    if (!obj->IsJSObject())
      return obj;

    // if aborted return NULL to terminate current transaction
    if (aborted_) {
      return Handle<Object>::null();
    }

    // lookup in write set and return if included
    Handle<Object> redirect = write_set_.Get(obj);
    if (!redirect.is_null()) {
      return redirect;
    }

    // make a copy
    JSObject* copy;
    { JSObject* jsObj = JSObject::cast(*obj);
      MaybeObject* maybe_result = isolate_->heap()->CopyJSObject(jsObj);
      if (!maybe_result->To<JSObject>(&copy)) return Handle<Object>();
    }

    // include it in write set and return
    ScopedLock lock(mutex_);
    return write_set_.Add(obj, copy);
  }

  void CommitHeap() {
    // copy all objects included in write set back to their original location
    Heap* heap = isolate_->heap();
    write_set_.CommitChanges(heap);
  }

  bool HasConflicts(Transaction* other) {
    if (read_set_.Intersects(other->write_set_))
      return true;

    if (write_set_.Intersects(other->write_set_))
      return true;

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
  ReadSet read_set_;
  WriteSet write_set_;
  Mutex* mutex_;
};

STM::STM() :
  heap_mutex_(OS::CreateMutex()),
  commit_mutex_(OS::CreateMutex()),
  transactions_mutex_(OS::CreateMutex()) {
}

void STM::EnterAllocationScope() {
  heap_mutex_->Lock();
}

void STM::LeaveAllocationScope() {
  heap_mutex_->Unlock();
}

void STM::EnterCollectionScope() {
  heap_mutex_->Lock();
}

void STM::LeaveCollectionScope() {
  heap_mutex_->Unlock();
}

Handle<Object> STM::RedirectLoad(Handle<Object> obj) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectLoad(obj);
}

Handle<Object> STM::RedirectStore(Handle<Object> obj) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectStore(obj);
}

void STM::StartTransaction() {
  Transaction* trans = new Transaction(isolate_);
  isolate_->set_transaction(trans);

  ScopedLock transactions_lock(transactions_mutex_);
  transactions_.Add(trans);
}

static bool even = true;

bool STM::CommitTransaction() {
  Transaction* trans = isolate_->get_transaction();
  ASSERT_NOT_NULL(trans);

  // for testing - abort each other transaction
  if (even) {
    trans->Abort();
  }
  even = ! even;

  ScopedLock commit_lock(commit_mutex_);
  ScopedLock transactions_lock(transactions_mutex_);

  bool comitted = false;

  // if the transaction was aborted then clear exceptions flag
  // so that it is not transferred to next attempt
  if (trans->IsAborted()) {
    trans->ClearExceptions();
  } else {
    // lock all transactions
    for (int i = 0; i < transactions_.length(); i++) {
      transactions_[i]->Lock();
    }

    // intersect write set with other transactions
    // abort those in conflict
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

} } // namespace v8::internal
