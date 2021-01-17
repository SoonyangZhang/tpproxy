#pragma once
#include <stdint.h>
namespace basic{
class IntervalBudget {
public:
    explicit IntervalBudget(int initial_target_rate_kbps);
    IntervalBudget(int initial_target_rate_kbps, bool can_build_up_underuse);
    void set_target_rate_kbps(int target_rate_kbps);
    
    // TODO(tschumim): Unify IncreaseBudget and UseBudget to one function.
    void IncreaseBudget(int64_t delta_time_ms);
    void UseBudget(int64_t bytes);
    
    int64_t bytes_remaining() const;
    double budget_ratio() const;
    int target_rate_kbps() const;
    bool IsStarted() const {return started_;}
private:
    bool started_=false;
    int target_rate_kbps_;
    int64_t max_bytes_in_budget_;
    int64_t bytes_remaining_;
    bool can_build_up_underuse_;
};
}