# About psync

psync is a **zero-dependency, high-performance synchronization library** for Linux x86-64 designed for systems programmers who need **minimal overhead and maximum throughput** in latency-critical applications.

## 🎯 Mission

Provide **first-principles synchronization primitives** without STL dependencies, runtime libraries, or external frameworks. Just raw CPU instructions and OS primitives for maximum control and performance.

## 💡 Vision

Enable developers to build **low-latency, high-throughput systems** (real-time trading, game engines, databases, network servers) with synchronization primitives that don't compromise on performance.

## 🛠 What We Build

- **Custom mutexes** (adaptive spinning, exponential backoff, futex sleep)
- **Reader/writer locks** (shared mutex with starvation resistance)
- **Sequence locks** (wait-free readers for read-mostly workloads)
- **Batch synchronization** (minimize lock/unlock churn)
- **Tagged pointers** (zero-byte overhead locks using alignment bits)
- **Condition variables** (FUTEX_CMP_REQUEUE for thundering herd mitigation)
- **Thread-safe telemetry** (serialized CPU cycle counters, zero-cost when disabled)

## 🎨 Design Philosophy

> **"General-purpose by API, specialized for low-overhead and high-throughput synchronization, with first-class support for batching workloads."**

We believe that **standard library synchronization primitives** are not sufficient for **latency-critical applications**. psync provides **general-purpose APIs** but is **specialized for performance** where it matters most.

## 🌟 Why psync?

- **Zero dependencies** (no Boost, Folly, Abseil, TBB)
- **Zero STL synchronization** (no `std::mutex`, `std::condition_variable`)
- **Custom atomics** (compiler builtins directly exposed)
- **Linux futex operations** (direct `SYS_futex` syscalls)
- **Adaptive spinning** (exponential backoff for optimal performance)
- **Thundering herd mitigation** (FUTEX_CMP_REQUEUE in kernel space)
- **High-throughput batching** (RAII guards for batch operations)
- **Thread-safe telemetry** (serialized CPU cycle counters)

## 🎯 Target Users

- **Systems programmers** building latency-critical applications
- **Game engine developers** needing entity component systems and physics simulations
- **High-frequency trading** platforms requiring minimal lock acquisition latency
- **Database developers** implementing B-tree locks and WAL serialization
- **Network server developers** optimizing connection pools and request batching
- **Embedded systems programmers** working with bare-metal synchronization

## 📚 Values

- **Correctness first**: Adversarial review, deadlock prevention, lost wakeup prevention
- **Honest measurements**: No misleading benchmarks, real-world workload testing
- **Zero magic**: Transparent code, no hidden abstractions, explicit memory ordering
- **Documentation**: Comprehensive design docs, adversarial review, implementation notes
- **Community**: Open source, contributions welcome, learning resources provided

## 🔮 Future Roadmap

- [ ] Windows condition variable optimization (native Win32 condition variables)
- [ ] RCU (Read-Copy-Update) support
- [ ] Lock-free ring buffers
- [ ] Hazard pointers for memory reclamation
- [ ] NUMA-aware synchronization
- [ ] ARM64 support
- [ ] Comprehensive benchmark suite
- [ ] Production deployment case studies

## 📊 Metrics

- **Lines of code**: ~2,000 (headers only)
- **Dependencies**: 0 (zero external dependencies)
- **Test coverage**: Basic tests + torture tests (high-contention scenarios)
- **Platform**: Linux x86-64 (primary), Windows (fallback)
- **Compiler**: GCC 7+, Clang 5+

## 🤝 Contributing

We welcome contributions! Please read the [Design Document](DESIGN.md) and [Adversarial Review](ADVERSARIAL_REVIEW.md) before contributing. Run tests (`make && ./psync_test && ./psync_torture`) and ensure ThreadSanitizer passes.

## 📄 License

MIT License - See [LICENSE](LICENSE) for details

---

**psync: Synchronization from first principles. Zero overhead. Maximum throughput.**