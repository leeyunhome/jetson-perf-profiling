# x86 서버에서 `function_graph` 재현 — Tegra 커널 제약의 대조군

## 이 문서의 목적

[`시행착오_기록.md`](시행착오_기록.md) 6번은 "Tegra 커널이 `function_graph` tracer를 컴파일에서 제외해서 못 썼다"고 기록만 하고 끝났다. 이 문서는 **표준 커널에서는 실제로 어떻게 동작하는지**를 별도의 x86 머신에서 확인해 그 빈칸을 메운다.

## 측정 환경 (Jetson과의 대조)

| 항목 | Jetson (본 저장소 주 실험) | x86 서버 (이 문서) |
|---|---|---|
| CPU | ARM (Tegra, Cortex 계열), 6 cores | Intel Core i7-7700K, 4코어/8스레드 @ 4.2GHz |
| 메모리 | — | 15Gi (측정 시점 여유 9.7Gi, swap 3.3/4.0Gi 사용 중) |
| Kernel | `6.8.12-1021-tegra` (NVIDIA 벤더 커스텀) | `6.17.0-19-generic` (**표준 Ubuntu 커널**) |
| OS | L4T 기반 Ubuntu 24.04 | Ubuntu 24.04.4 LTS |
| perf | 대응 패키지 없음 → 우회 필요 ([`시행착오_기록.md`](시행착오_기록.md) 3번) | `perf version 6.17.13` — **네이티브로 바로 동작** |
| ftrace `function_graph` | `available_tracers`에 없음 | **있음** (`timerlat osnoise hwlat blk mmiotrace function_graph wakeup_dl wakeup_rt wakeup function nop`) |
| `/tmp`(테스트 파일 위치) | ext4 (구체적 매체 미확인) | ext4, **회전식 HDD**(`lsblk ROTA=1`) — SSD/NVMe 아님 |
| 용도 | 단독 개인 임베디드 장비 | **다중 사용자 공유 GPU 서버** (딥러닝 학습/추론용, 다른 프로세스 상시 구동) |

**공유 서버라는 조건이 이 실습의 방법론에 실제로 영향을 줬다** — 아래 "안전 수칙" 절 참고.

## 결론 먼저: `function_graph`는 유저스페이스 함수를 추적하지 못한다

착수 전에 정정해야 했던 오해: ftrace의 `function_graph`는 **커널에 컴파일된 함수**만 추적한다 (`fentry`/`mcount` 계측 기반). Jetson 실습의 `fib()` 같은 유저스페이스 함수는 애초에 대상이 아니다 — Tegra에 `function_graph`가 있었다 해도 `fib()`를 추적할 수는 없었다.

그래서 대신 **`write()` 시스템콜이 커널 안에서 실제로 어떤 함수를 거치는지**를 추적 대상으로 삼았다. Jetson에서는 `raw_syscalls:sys_enter/sys_exit` tracepoint로 "syscall이 시작/끝났다"는 것만 봤는데(시행착오 기록에서 다룸), `function_graph`는 **그 안의 커널 함수 호출 트리 전체**를 보여준다 — Tegra 벤더 커널이 정확히 막아놨던 능력이다.

## 시행착오: 세 번 시도 중 두 번 실패 (안전과 직결됨)

### 1차 시도 — 빈 캡처 (0건)

```bash
sudo sh -c 'echo nop > .../current_tracer'
sudo sh -c 'echo ksys_write > .../set_graph_function'
sudo sh -c 'echo function_graph > .../current_tracer'
```

`set_graph_function`을 먼저 설정하고 `current_tracer`를 나중에 `function_graph`로 전환했더니 **0건** 캡처됨. 원인 추정: tracer를 전환하는 동작 자체가 그래프 필터를 초기화하는 것으로 보임.

### 2차 시도 — 스코프 없이 시스템 전체를 무제한 추적 (위험했던 순간)

순서를 바꿔 `current_tracer`만 다시 `function_graph`로 전환하고 필터를 재확인하지 않은 채 `dd`를 실행했다. 결과: **`/tmp/fg.txt`가 46,582,000줄.** `set_graph_function`이 비어 있었던 것으로 보이며, 이 경우 `function_graph`는 **시스템의 모든 커널 함수 호출**을 8코어 전체에서 추적한다 — 짧은 시간이었지만 이 서버가 **다른 사람도 쓰는 공유 GPU 서버**라는 점에서 실제 위험이었다 (다른 프로세스에도 트레이싱 오버헤드가 그대로 걸림).

`Ctrl+C`는 `cat`만 끊었을 뿐 트레이서 자체는 계속 켜져 있었다 — 별도로 `current_tracer nop`, `tracing_on 0`을 명시적으로 실행해야 완전히 꺼진다는 걸 이때 배웠다. 확인 결과 `uptime`(load average 0.50), `nvidia-smi` 모두 정상이라 실질적 피해는 없었다.

### 3차 시도 — 매 단계 검증 후 성공

원인(전환 순서에 따른 필터 초기화)을 반영해, **tracer 설정 → 확인 → 필터 설정 → 확인 → `tracing_on` 켜기 → 워크로드 실행 → 즉시 끄기** 순으로 매 단계 값을 직접 확인하며 진행. `current_tracer`가 정확히 `function_graph`로, `set_graph_function`이 정확히 `ksys_write`로 확인된 뒤에만 `tracing_on 1`을 실행했다. 결과: 2,210줄 — 정상 규모로 캡처 성공.

**교훈**: 공유 시스템에서 전역 커널 상태(ftrace)를 건드릴 땐, "설정하고 바로 실행"이 아니라 "설정 → 확인 → 실행"을 매번 거쳐야 한다. 격리된 개인 장비(Jetson)에서는 이런 실수가 나 하나만 불편하게 하지만, 공유 서버에서는 다른 사람의 작업에 영향을 줄 수 있다.

## 캡처된 실제 호출 트리 (`write()` 내부)

`dd if=/dev/zero of=./test4.bin bs=64 count=3 conv=fsync` 실행 중 잡힌 트리 일부:

```
ksys_write() {
  fdget_pos();
  vfs_write() {
    rw_verify_area() {
      security_file_permission() {
        apparmor_file_permission() {          <- AppArmor 보안 검사 (Jetson에는 없던 계층)
          common_file_perm() {
            aa_file_perm() { ... }
          }
        }
      }
    }
    ext4_file_write_iter() {
      ext4_buffered_write_iter() {
        down_write() { ... }                   <- inode 락 획득
        ext4_generic_write_checks() { ... }
        file_modified() {
          file_remove_privs_flags() { ... }
          inode_needs_update_time.part.0() { ... }
        }
        generic_perform_write() {
          balance_dirty_pages_ratelimited() { ... }   <- dirty page 스로틀링
          ext4_da_write_begin() {
            __filemap_get_folio() { ... }              <- 페이지 캐시 폴리오 할당
            ...
          }
        }
      }
    }
  }
}
```

전체 원본은 [`docs/x86_function_graph_write.txt`](x86_function_graph_write.txt) 참고.

이게 바로 Jetson에서 `raw_syscalls:sys_enter/exit`로는 볼 수 없었던 부분이다 — `write()`가 "시작하고 끝났다"만 아니라, **AppArmor 보안 검사 → ext4 inode 락 → dirty page 스로틀링 → 페이지 캐시 할당**까지 순서대로 거친다는 걸 함수 단위로 확인할 수 있다.

## 예상 밖의 발견: 41건 중 우리 것은 1건뿐

캡처된 2,210줄 안에 `ksys_write() {` 진입이 총 **41번** 있었다. 그런데 `dd` 프로세스와 확실히 연결된 건 컨텍스트 스위치 마커(`sudo-N => dd-N`) 바로 뒤에 나온 **딱 1건**뿐이었다. 나머지 40건은 이 서버에서 상시 구동 중인 무관한 프로세스들의 write였다:

- `nxserve` (NoMachine 원격 데스크톱 서버)
- `rs:main` (rsyslog)
- `gmain` (GTK/GLib 기반 앱의 메인 루프)
- `sshd`, `bash`, `sh`, `sudo` (이 실습 자체의 SSH 세션 활동)

**이게 [`시행착오_기록.md`](시행착오_기록.md) 10번("pid 필터링 없이 트레이싱하면 노이즈에 신호가 묻힘")의 완전히 독립적인 재확인 사례다.** Jetson에서는 임베디드 단일 사용자 환경이라 노이즈가 커널 스레드(`kworker`, `irq`) 위주였는데, 여기서는 **다중 사용자 데스크톱/서버 환경이라 노이즈의 정체 자체가 다르다** — 원격 접속 데몬, 로그 데몬, GUI 앱까지 섞인다. "노이즈 필터링이 필요하다"는 결론은 같지만, 어떤 프로세스가 노이즈원이 되는지는 환경에 따라 완전히 다르다는 걸 보여준다.

추가로 발견한 것: **`function_graph`의 기본 출력 포맷은 컨텍스트 스위치 지점에서만 태스크명을 찍는다.** 그 사이에 낀 줄들은 어느 프로세스 것인지 줄만 봐서는 알 수 없다 — `trace_options`의 `funcgraph-proc` 옵션을 켜거나, 애초에 `set_ftrace_pid`로 커널 레벨에서 필터링해야 신뢰성 있게 귀속시킬 수 있다. Jetson에서 배운 "pid 필터링" 교훈이 `function_graph`에서는 한 단계 더 나아가 "출력 포맷 자체가 귀속 정보를 기본으로 안 준다"는 형태로 다시 나타난 것.

## 종합

1. `function_graph`는 유저스페이스 함수가 아니라 **커널 함수**를 추적한다 — Tegra에 있었어도 우리 `fib()`는 못 봤을 것이다.
2. 표준 커널(x86)에서는 정상 동작하며, Tegra가 막아놨던 "syscall 내부의 실제 커널 함수 호출 트리"를 볼 수 있다.
3. **전역 커널 트레이싱 상태를 다루는 명령 순서는 결과에 직접 영향을 준다** — 설정과 실행 사이에 검증 단계 없이 진행하다 시스템 전체를 무제한으로 추적하는 사고가 날 뻔했다. 공유 서버에서는 이게 단순 실수가 아니라 다른 사용자에게 영향을 줄 수 있는 리스크다.
4. pid 필터링 없는 트레이싱의 노이즈 문제는 Jetson과 x86 양쪽에서 독립적으로 재현됐다 — 다만 노이즈의 정체(임베디드: 커널 스레드/인터럽트, 데스크톱 서버: 원격 데스크톱·로그 데몬 등 사용자 프로세스)는 환경마다 다르다.
