#include "v8.h"
#include "stm.h"
#include "isolate.h"

#include <set>
#include <map>

namespace v8 {
namespace internal {

// collection of cells, designed with the following objectives:
// - fast lookup for pointer to owned cell
// - fast mapping from object address to owned cell pointer
// - lookup and mapping can be implemented in assembler (in the future)
// - does not relocate cells (because handles point to the them)
// - supports updates of object addresses from GC
//
// notes:
// - current implementaion is not optimized yet (N^2 complexity!)
// - cell can point to the same object (in read set) or to a copy (write set)
// - original address and mapped cell in read set point to the same object
// - both original object and our copy are retained in memory by our pointers
// - all cells are destroyed when transaction ends (no need for handle scopes)
// - we have to keep own cells in read set because original cells can be
//   destroyed by handle scope

class CellMap {
 public:
  CellMap() :
    first_block_(NULL), last_block_address_(&first_block_) {
  }

  ~CellMap() {
    Block* block = first_block_;

    while (block != NULL) {
      Block* next = block->next_;
      free(block);
      block = next;
    }

    first_block_ = NULL;
    last_block_address_ = &first_block_;
  }

  void RebuildObjectMap() {
    object_map_.clear();

    Block* block = first_block_;

    while (block != NULL && block != *last_block_address_) {
      for (int i = 0; i < BLOCK_SIZE; i++) {
        CellPair& pair = block->cells_[i];
        object_map_.insert(ObjectMap::value_type(pair.from_, &pair.to_));
      }
      block = block->next_;
    }

    if (block != NULL) { // last block
      for (int i = 0; i < index_; i++) {
        CellPair& pair = block->cells_[i];
        object_map_.insert(ObjectMap::value_type(pair.from_, &pair.to_));
      }
    }
  }

  void Iterate(ObjectVisitor* v) {
    // notes:
    // - location_set_ doesn't need invalidation because cells don't move

    bool changes = false;

    Block* block = first_block_;

    while (block != NULL && block != *last_block_address_) {
      for (int i = 0; i < BLOCK_SIZE; i++) {
        CellPair& pair = block->cells_[i];
        Object* old_from = pair.from_;

        v->VisitPointer(&pair.from_);
        v->VisitPointer(&pair.to_);

        changes = changes || (pair.from_ != old_from);
      }
      block = block->next_;
    }

    if (block != NULL) { // last block
      for (int i = 0; i < index_; i++) {
        CellPair& pair = block->cells_[i];
        Object* old_from = pair.from_;

        v->VisitPointer(&pair.from_);
        v->VisitPointer(&pair.to_);

        changes = changes || (pair.from_ != old_from);
      }
    }

    if (changes) {
      RebuildObjectMap();
    }
  }

  bool IsMapped(Object** location) {
    return location_set_.find(location) != location_set_.end();
  }

  Object** GetMapping(Object* object) {
    ObjectMap::const_iterator it = object_map_.find(object);
    if (it != object_map_.end()) {
      return it->second;
    }

    return NULL;
  }

  Object** AddMapping(Object* object, Object* redirect) {
    if (*last_block_address_ == NULL) {
      *last_block_address_ = (Block*)malloc(sizeof(Block));
      (*last_block_address_)->next_ = NULL;
      index_ = 0;
    }

    // allocate new cell
    CellPair& pair = (*last_block_address_)->cells_[index_];
    pair.from_ = object;
    pair.to_ = redirect;
    index_++;

    location_set_.insert(&pair.to_);
    object_map_.insert(ObjectMap::value_type(object, &pair.to_));

    // block is full
    if (index_ == BLOCK_SIZE) {
      last_block_address_ = &(*last_block_address_)->next_;
    }

    return &pair.to_;
  }

  void CommitChanges(Heap* heap) {
    Block* block = first_block_;

    while (block != NULL && block != *last_block_address_) {
      for (int i = 0; i < BLOCK_SIZE; i++) {
        CellPair& pair = block->cells_[i];
        ASSERT(pair.from_->IsHeapObject());
        ASSERT(pair.to_->IsHeapObject());
        Address dst = reinterpret_cast<HeapObject*>(pair.from_)->address();
        Address src = reinterpret_cast<HeapObject*>(pair.to_)->address();
        int size = reinterpret_cast<HeapObject*>(pair.from_)->Size();
        heap->CopyBlock(dst, src, size);
      }
      block = block->next_;
    }

    if (block != NULL) { // last block
      for (int i = 0; i < index_; i++) {
        CellPair& pair = block->cells_[i];
        ASSERT(pair.from_->IsHeapObject());
        ASSERT(pair.to_->IsHeapObject());
        Address dst = reinterpret_cast<HeapObject*>(pair.from_)->address();
        Address src = reinterpret_cast<HeapObject*>(pair.to_)->address();
        int size = reinterpret_cast<HeapObject*>(pair.from_)->Size();
        heap->CopyBlock(dst, src, size);
      }
    }
  }

  bool Intersects(const CellMap& other) {
    Block* block = other.first_block_;

    while (block != NULL && block != *other.last_block_address_) {
      for (int i = 0; i < BLOCK_SIZE; i++) {
        CellPair& pair = block->cells_[i];
        if (GetMapping(pair.from_) != NULL) {
          return true;
        }
      }
      block = block->next_;
    }

    if (block != NULL) { // last block
      for (int i = 0; i < other.index_; i++) {
        CellPair& pair = block->cells_[i];
        if (GetMapping(pair.from_) != NULL) {
          return true;
        }
      }
    }

    return false;
  }

 private:
  struct CellPair {
    Object* from_;
    Object* to_;
  };

  const static int BLOCK_SIZE = 100;

  struct Block {
    CellPair cells_[BLOCK_SIZE];
    Block* next_;
  };

  Block*  first_block_;
  Block** last_block_address_;
  int index_;

  typedef std::set<Object**> LocationSet;
  typedef std::map<Object*, Object**> ObjectMap;

  LocationSet location_set_;
  ObjectMap object_map_;
};

class WriteSet {
 public:
  void Iterate(ObjectVisitor* v) {
    // interate all handles (they may be updated)
    // update our sets if handles were updated
    map_.Iterate(v);
  }

  Handle<Object> Get(Handle<Object> obj) {
    // 1) it is our handle (already redirected)
    if (map_.IsMapped(obj.location())) {
      return obj;
    }

    // 2) we have a cell for the address of a copy of this object
    Object** location = map_.GetMapping(*obj);
    if (location != NULL) {
      return Handle<Object>(location);
    }

    return Handle<Object>::null();
  }

  Handle<Object> Add(Handle<Object> obj, Object* redirect) {
    // create a cell for the redirected object
    Object** location = map_.AddMapping(*obj, redirect);
    ASSERT_NOT_NULL(location);
    return Handle<Object>(location);
  }

  void CommitChanges(Heap* heap) {
    map_.CommitChanges(heap);
  }

  bool Intersects(const WriteSet& other) {
    return map_.Intersects(other.map_);
  }
  
 private:
  CellMap map_;

  friend class ReadSet;
};

class ReadSet {
 public:
  void Iterate(ObjectVisitor* v) {
    map_.Iterate(v);
  }

  Handle<Object> Get(Handle<Object> obj) {
    // 1) it is our handle (already redirected)
    if (map_.IsMapped(obj.location())) {
      return obj;
    }

    // 2) we have our own handle for this object
    Object** location = map_.GetMapping(*obj);
    if (location != NULL) {
      return Handle<Object>(location);
    }

    return Handle<Object>::null();
  }

  Handle<Object> Add(Handle<Object> obj) {
    // create handle pointing to the same object
    Object** location = map_.AddMapping(*obj, *obj);
    ASSERT_NOT_NULL(location);
    return Handle<Object>(location);
  }

  bool Intersects(const WriteSet& other) {
    return map_.Intersects(other.map_);
  }

 private:
  CellMap map_;
};

class Transaction {
 public:
  Transaction(Isolate* isolate) :
    aborted_(false),
    isolate_(isolate),
    mutex_(OS::CreateMutex()),
    gc_mutex_(OS::CreateMutex()),
    done_gc_(NULL) {
    gc_mutex_->Lock();
  }

  void Iterate(ObjectVisitor* v) {
    read_set_.Iterate(v);
    write_set_.Iterate(v);
  }

  Handle<Object> RedirectLoad(Handle<Object> obj, bool* terminate) {
    ASSERT(!obj.is_null());

    if (!obj->IsJSObject() || obj->IsJSFunction()) {
      return obj;
    }

    if (aborted_) {
      *terminate = true;
      return obj;
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

  Handle<Object> RedirectStore(Handle<Object> obj, bool* terminate) {
    ASSERT(!obj.is_null());

    // TODO: handle functions too (Heap::CopyJSObject doesn't accept them)
    if (!obj->IsJSObject() || obj->IsJSFunction()) {
      return obj;
    }

    if (aborted_) {
      *terminate = true;
      return obj;
    }

    // lookup in write set and return if included
    Handle<Object> redirect = write_set_.Get(obj);
    if (!redirect.is_null()) {
      return redirect;
    }

    // make a copy
    JSObject* copy = CreateCopy(obj);
    if (copy == NULL) {
      FATAL("Cannot create object copy");
      aborted_ = true;
      *terminate = true;
      return obj;
    }

    // include it in write set and return
    ScopedLock lock(mutex_);
    return write_set_.Add(obj, copy);
  }

  JSObject* CreateCopy(Handle<Object> obj) {
    // obj will be included in the root list becuase it is used on stack
    CALL_AND_RETRY(isolate_,
      isolate_->heap()->CopyJSObject(JSObject::cast(*obj)),
      return JSObject::cast(__object__),
      return NULL);
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

  void LockGC() { gc_mutex_->Lock(); }
  void UnlockGC() { gc_mutex_->Unlock(); }

  void ResetDoneGC() {
    ASSERT(done_gc_ == NULL);
    done_gc_ = OS::CreateSemaphore(0);
  }

  void WaitDoneGC() {
    ASSERT_NOT_NULL(done_gc_);
    done_gc_->Wait();
    delete done_gc_;
    done_gc_ = NULL;
  }

  void SignalDoneGC() {
    if (done_gc_ != NULL) {
      done_gc_->Signal();
    }
  }

 private:
  volatile bool aborted_;
  Isolate* isolate_;
  ReadSet read_set_;
  WriteSet write_set_;
  Mutex* mutex_;
  Mutex* gc_mutex_;
  Semaphore* done_gc_;
};

STM::STM() :
  need_gc_(0),
  heap_mutex_(OS::CreateMutex()),
  commit_mutex_(OS::CreateMutex()),
  transactions_mutex_(OS::CreateMutex()) {
}

// we respect the following requirements
// - all heap modifications (allocs and GCs) should be mutually exclusive
// - when GC is needed in at least one thread other threads must be stopped in a
//   safe state (all object pointers are tracked)
// - when several threads run out of memory at the same time (highly probable
//   situation) one GC should be enought for all of them
// - collection scope can be nested into allocation scope (one level only)
// - need adapt to the number of active transactions
// - GC is relatively rare event and it shouldn't slow down everything
//
// we implement them in the following way
// - each transaction has a lock that must be acquired before GC
// - GC thread prevents modification of transactions list via
//   `transactions_mutex_`
// - transactions just starting cannot modify heap so we simply block them
// - committing transactions release GC lock before (possibly) blocking on
//   `transactions_mutex_`
// - each thread checks a flag before each allocation and pauses if GC is
//   required

void STM::EnterAllocationScope() {
  if (!v8::internal::FLAG_stm) {
    return;
  }

  PauseForGC();
  heap_mutex_->Lock();
}

void STM::LeaveAllocationScope() {
  if (!v8::internal::FLAG_stm) {
    return;
  }

  heap_mutex_->Unlock();
}

bool STM::EnterCollectionScope() {
  if (!v8::internal::FLAG_stm) {
    return true;
  }

  // signal that we need a GC
  Atomic32 need_gc_prev_;
  need_gc_prev_ = NoBarrier_CompareAndSwap(&need_gc_, 0, 1);
  if (need_gc_prev_ != 0) {
    // GC is pending in another thread so we simply allow it to proceed
    PauseForGC();
    return false;
  }

  // make sure that transaction list cannot be changed
  // transactions are either blocked by `transactions_mutex_` or paused for GC
  transactions_mutex_->Lock();

  // wait for other threads to pause
  Transaction* current_trans = isolate_->get_transaction();
  ASSERT_NOT_NULL(current_trans);
  for (int i = 0; i < transactions_.length(); i++) {
    Transaction* trans = transactions_[i];
    if (trans == current_trans) { continue; }

    // if transaction is locked in commit by `transactions_mutex_` then it has
    // released GC lock already and we are fine, otherwise it will pause for GC
    // or enter commit eventually
    // we don't care about newly starting transactions
    trans->LockGC();
  }

  // TODO(w16): do we need the barrier here?
  MemoryBarrier();

  // do GC
  return true;
}

void STM::LeaveCollectionScope() {
  if (!v8::internal::FLAG_stm) {
    return;
  }

  // enable future GCs
  Atomic32 need_gc_prev_;
  need_gc_prev_ = NoBarrier_CompareAndSwap(&need_gc_, 1, 0);
  ASSERT(need_gc_prev_ == 1);

  // signal other threads to resume
  Transaction* current_trans = isolate_->get_transaction();
  ASSERT_NOT_NULL(current_trans);
  for (int i = 0; i < transactions_.length(); i++) {
    Transaction* trans = transactions_[i];
    if (trans == current_trans) { continue; }

    // if transaction is paused for GC then we'll release it by SignalDoneGC
    // if it was blocked by `transactions_mutex_` then we allow it to proceed
    // after acquiring `transactions_mutex_` by unlocking GC
    trans->UnlockGC();
    trans->SignalDoneGC();
  }

  // allow transactions list to be modified
  transactions_mutex_->Unlock();
}

void STM::PauseForGC() {
  if (!need_gc_) {
    return;
  }

  Transaction* trans = isolate_->get_transaction();
  ASSERT_NOT_NULL(trans);

  // signal that we paused
  trans->ResetDoneGC();
  trans->UnlockGC();

  // wait for GC to complete
  trans->WaitDoneGC();
  trans->LockGC();
}

void STM::Iterate(ObjectVisitor* v) {
  for (int i = 0; i < transactions_.length(); i++) {
    transactions_[i]->Iterate(v);
  }
}

Handle<Object> STM::RedirectLoad(Handle<Object> obj, bool* terminate) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectLoad(obj, terminate);
}

Handle<Object> STM::RedirectStore(Handle<Object> obj, bool* terminate) {
  Transaction* trans = isolate_->get_transaction();
  if (trans == NULL) {
    return obj;
  }

  return trans->RedirectStore(obj, terminate);
}

void STM::StartTransaction() {
  Transaction* trans = new Transaction(isolate_);
  isolate_->set_transaction(trans);

  ScopedLock transactions_lock(transactions_mutex_);
  transactions_.Add(trans);
}

bool STM::CommitTransaction() {
  Transaction* trans = isolate_->get_transaction();
  ASSERT_NOT_NULL(trans);

  // for testing - abort each other transaction
  static bool even = true;
  if (!even && FLAG_stm_aborts) {
    trans->Abort();
  }
  even = ! even;

  // thread might be blocked here so we need to allow GC to proceed
  trans->UnlockGC();
  ScopedLock commit_lock(commit_mutex_);
  ScopedLock transactions_lock(transactions_mutex_);
  trans->LockGC();

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
