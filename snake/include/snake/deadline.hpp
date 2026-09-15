#pragma once

/// @file deadline.hpp
/// Time manager. El timeout que anuncia el request INCLUYE la latencia de red, asi
/// que el presupuesto de computo es `timeout - network_margin - safety_margin`,
/// acotado ademas por `max_compute_ms`.
/// ver docs/performance.md#p-01

#include <chrono>

#include <snake/params.hpp>

namespace snake {

class Deadline {
public:
    using Clock = std::chrono::steady_clock;

    /// Deadline ya calculado, para tests con presupuestos artificiales.
    explicit Deadline(Clock::time_point end) noexcept : end_(end) {}

    /// Deadline derivado del timeout del request y de los margenes del config.
    static Deadline from_timeout(std::int32_t timeout_ms,
                                 const TimeParams& time,
                                 Clock::time_point start = Clock::now()) noexcept {
        std::int32_t budget = timeout_ms - time.network_margin_ms - time.safety_margin_ms;
        if (budget > time.max_compute_ms) {
            budget = time.max_compute_ms;
        }
        // Un timeout absurdamente bajo no debe producir un deadline en el pasado: el
        // cerebro necesita al menos un barrido de las 4 direcciones.
        if (budget < 1) {
            budget = 1;
        }
        return Deadline(start + std::chrono::milliseconds(budget));
    }

    [[nodiscard]] bool expired() const noexcept { return Clock::now() >= end_; }

    [[nodiscard]] std::int64_t remaining_ms() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end_ - Clock::now()).count();
    }

    [[nodiscard]] Clock::time_point end() const noexcept { return end_; }

private:
    Clock::time_point end_;
};

} // namespace snake
