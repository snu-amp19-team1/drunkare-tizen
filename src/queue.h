#include <vector>
#include <deque>
#include <iostream>
#include <atomic>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <pthread.h>

// Concurrent FIFO queue
template <typename T>
struct Queue {
  Queue() /* : _done(false) */ {};

  std::unique_ptr<T> dequeue() {
    // std::unique_lock<std::mutex> lk(m);
    pthread_mutex_lock(&this->m);

    // bool done = false;
    while (this->container.empty() /* && !done */) {
      pthread_cond_wait(&this->cv, &this->m);
      // done = _done.load();
    }

    // if (done) {
    //   pthread_mutex_unlock(&this->m);
    //   return nullptr;
    // }

    // cv.wait(lk, [this, &done]() {
    //   done = this->_done.load();
    //   return !this->container.empty() || done;
    // });

    // if (done) {
    //   lk.unlock();
    //   return nullptr;
    // }

    auto result = std::move(container.front());
    container.pop_front();
    // lk.unlock();
    pthread_mutex_unlock(&this->m);
    return result;
  }

  void enqueue(std::unique_ptr<T> data) {
    // NOTE: Nofify first, unlock later!
    // https://docs.oracle.com/cd/E19455-01/806-5257/6je9h032r/index.html

    // std::unique_lock<std::mutex> lk(m);
    pthread_mutex_lock(&this->m);
    container.push_back(std::move(data));
    // cv.notify_all();
    pthread_cond_broadcast(&this->cv);
    // lk.unlock();
    pthread_mutex_unlock(&this->m);
  }

  void forceDone() {
    // _done.store(true);
    // cv.notify_all();
    pthread_mutex_lock(&this->m);
    this->container.clear();
    pthread_cond_broadcast(&this->cv);
    pthread_mutex_unlock(&this->m);
  }

  pthread_mutex_t m;
  pthread_cond_t cv;
  // std::atomic<bool> _done;
  std::deque<std::unique_ptr<T>> container;
};
