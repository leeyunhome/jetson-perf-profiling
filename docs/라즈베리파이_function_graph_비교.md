# 라즈베리파이에서 다시 확인한 `function_graph` — 두 번째 독립 대조군

## 이 문서의 목적

[`docs/x86_function_graph_비교.md`](x86_function_graph_비교.md)에서 x86 서버로 확인했던 것("Tegra 벤더 커널만
`function_graph`가 빠진 걸까, 아니면 흔한 패턴일까")을 **진짜 ARM 임베디드 보드**에서 한 번 더 검증했다.
x86은 아키텍처 자체가 다르니 완전한 대조군이 못 됐는데, 이번엔 같은 ARM 계열이라 훨씬 직접적인 비교가 된다.

## 측정 환경

| 항목 | Jetson (주 실험) | x86 서버 (부록1) | Raspberry Pi (부록2, 이 문서) |
|---|---|---|---|
| CPU | ARM (Tegra) | Intel i7-7700K | ARM64 (Raspberry Pi) |
| Kernel | `6.8.12-1021-tegra` (NVIDIA 벤더 커스텀) | `6.17.0-19-generic` | `6.12.25+rpt-rpi-v8` (Raspberry Pi Foundation 표준 빌드) |
| OS | L4T 기반 Ubuntu 24.04 | Ubuntu 24.04.4 LTS | Debian (Raspbian) |
| `function_graph` | 없음 (`available_tracers`에 `nop`뿐) | 있음 | **있음** |
| 접근 방식 | 로컬 리포 작업 | SSH (deeplearning 서버) | SSH (`manager@192.168.2.157`) |

## 결과: 표준 ARM 커널에는 `function_graph`가 정상적으로 있다

```
$ sudo cat /sys/kernel/debug/tracing/available_tracers
blk function_graph wakeup_dl wakeup_rt wakeup function nop
```

x86 때와 같은 방식으로(`ksys_write`에 그래프 필터를 걸어 `write()` syscall의 커널 내부 경로를 추적), **검증 후 실행(verify-then-arm)** 절차를 지켜 안전하게 캡처했다.

```
$ sudo cat /sys/kernel/debug/tracing/current_tracer   # -> function_graph 확인 후에만 진행
$ sudo cat /sys/kernel/debug/tracing/set_graph_function  # -> ksys_write 확인 후에만 tracing_on
```

`dd if=/dev/zero of=~/ftrace-review/test.bin bs=64 count=3 conv=fsync` 실행 중 캡처된 콜스택 (총 910줄 — x86 때의 2,210줄보다 훨씬 적어서 노이즈도 적었다):

```
sh-2199  =>  dd-2200
------------------------------------------
ksys_write() {
  fdget_pos();
  vfs_write() {
    rw_verify_area() {
      security_file_permission();
    }
    ext4_file_write_iter() {
      ext4_buffered_write_iter() {
        down_write();
        ext4_generic_write_checks() {
          generic_write_checks() {
            generic_write_check_limits();
          }
        }
        file_modified() {
          file_remove_privs_flags() {
            setattr_should_drop_suidgid();
            security_inode_need_killpriv() {
              cap_inode_need_killpriv();
            }
          }
          inode_needs_update_time() {
            ktime_get_coarse_real_ts64();
            timestamp_truncate();
          }
        }
        generic_perform_write() {
          balance_dirty_pages_ratelimited() {
            balance_dirty_pages_ratelimited_flags() {
              inode_to_bdi();
              inode_to_bdi();
              __rcu_read_lock();
              __rcu_read_unlock();
              balance_dirty_pages();
            }
          }
          fault_in_readable();
          ext4_da_write_begin() { ... }
        }
      }
    }
  }
}
```

x86 서버에서 본 것과 **거의 동일한 커널 함수 트리**(`ext4_file_write_iter` → `generic_write_checks` → `file_modified` → `generic_perform_write` → `balance_dirty_pages_ratelimited`)가 그대로 나왔다. ext4의 buffered write 경로는 아키텍처와 무관하게 동일한 코드이니 당연한 결과지만, **실제로 재현해서 확인한 것과 "당연히 되겠지"라고 넘어가는 것은 다르다.**

## 결론

1. Jetson에서 `function_graph`가 빠진 건 **Tegra 벤더 커널만의 문제**다. 표준 ARM 빌드(Raspberry Pi Foundation 커널)에는 정상적으로 있다.
2. 이걸 **아키텍처가 다른 x86**과 **아키텍처가 같은 ARM** 양쪽에서 독립적으로 확인했다는 게 중요하다 — "벤더가 최소화 빌드에서 트레이싱 기능을 빼는 것"이 일반적인 패턴이지, 특정 아키텍처의 한계가 아니라는 걸 두 번 검증한 셈이다.
3. 안전 절차(설정 → 확인 → 실행 → 즉시 정리)를 한 번 더 지켰고, 이번엔 x86 때 같은 사고(필터 미확인으로 시스템 전체 추적) 없이 깔끔하게 끝냈다.
