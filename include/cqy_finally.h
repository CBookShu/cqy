#pragma once
#include <functional>
#include <utility>

namespace cqy {
  template<class F>
  class FinalAction {
  public:
      FinalAction(F f) noexcept: f_(std::move(f)), invoke_(true) {}
  
      FinalAction(FinalAction &&other) noexcept: f_(std::move(other.f_)), invoke_(other.invoke_) {
          other.invoke_ = false;
      }
  
      FinalAction(const FinalAction &) = delete;
  
      FinalAction &operator=(const FinalAction &) = delete;
  
      ~FinalAction() noexcept {
          if (invoke_) f_();
      }
  
  private:
      F f_;
      bool invoke_;
  };
  
  template<class F>
  inline FinalAction<F> _finally(const F &f) noexcept {
      return FinalAction<F>(f);
  }
  
  template<class F>
  inline FinalAction<F> _finally(F &&f) noexcept {
      return FinalAction<F>(std::forward<F>(f));
  }
  
  #define concat1(a, b)       a ## b
  #define concat2(a, b)       concat1(a, b)
  #define _finally_object     concat2(_finally_object_, __COUNTER__)
  #define finally             cqy::FinalAction _finally_object = [&]()
  #define finally2(func)      cqy::FinalAction _finally_object = cqy::_finally(func)
}