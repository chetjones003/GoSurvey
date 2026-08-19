// REQ-069 — the background surface-rebuild worker's lifetime.
//
// Regression for a crash on exit. AppCommandState::SurfaceRebuildAsync owns a std::thread, and the
// only join site (TickSurfaceRebuilds) is reachable ONLY once the job's `done` flag is already set.
// Nothing joined a job that was still running, so destroying one mid-flight destroyed a joinable
// std::thread — which calls std::terminate. `main()` holds AppCommandState as a local, so returning
// from it aborted the process instead of exiting; and since cadGpuRevision moves on every drawing
// mutation, an edit immediately followed by closing the window was enough to reach it.
//
// This test would not merely fail without the destructor — it would take the whole test binary down
// with it, which is exactly the severity being guarded.

#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

TEST_CASE("Destroying an in-flight surface rebuild joins its worker instead of terminating",
         "[surface][req069]") {
  std::atomic<bool> workerRan{false};
  std::atomic<bool> workerFinished{false};

  {
    std::vector<std::unique_ptr<AppCommandState::SurfaceRebuildAsync>> jobs;
    auto job = std::make_unique<AppCommandState::SurfaceRebuildAsync>();
    auto* p = job.get();
    p->surfaceName = "In flight";
    p->thread = std::thread([p, &workerRan, &workerFinished] {
      workerRan.store(true);
      // Long enough that the scope below is left while this is still running — the whole point.
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      workerFinished.store(true);
      p->done.store(true, std::memory_order_release);
    });
    jobs.push_back(std::move(job));

    // Leaving this scope destroys the job with its thread still running.
  }

  // Reaching here at all is the assertion: without the destructor's join the process is already gone.
  CHECK(workerRan.load());
  // The destructor must WAIT, not detach — a detached worker would still be writing into freed
  // memory after this point, which is the same defect wearing a quieter failure mode.
  CHECK(workerFinished.load());
}

TEST_CASE("A surface rebuild already reaped is destroyed without a second join", "[surface][req069]") {
  // TickSurfaceRebuilds joins before erasing, so the destructor then sees a non-joinable thread.
  // Joining twice is undefined behaviour, so the destructor's joinable() guard is load-bearing.
  auto job = std::make_unique<AppCommandState::SurfaceRebuildAsync>();
  job->thread = std::thread([&] { job->done.store(true, std::memory_order_release); });
  job->thread.join();  // stands in for TickSurfaceRebuilds' own join
  CHECK_FALSE(job->thread.joinable());
  job.reset();  // destructor must be a no-op here
  SUCCEED("destroyed an already-joined job without a second join");
}
