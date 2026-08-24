# Jetson 성능 분석 실습 로그

## 환경
- Device: Jetson Orin (Tegra), kernel `6.8.12-1021-tegra` (L4T 커스텀 커널)
- 목표: perf/ftrace로 실제 ARM 하드웨어에서 프로파일링 + Flame Graph 생성
  (이전에 VM에서는 PMU 하드웨어 이벤트가 가상화 제약으로 미지원이었음)

## 진행 기록

### 1. 저장소 셋업
- GitHub repo `leeyunhome/jetson-perf-profiling` 생성
- HTTPS push 시도 → GitHub는 비밀번호 인증 미지원(2021년 폐지)으로 실패
- 해결: `ed25519` 키 신규 생성 → repo 전용 Deploy Key(Read/write)로 등록 → `~/.ssh/config`에 `Host github.com` 항목 추가 → remote를 SSH(`git@github.com:...`)로 전환하여 해결
- `ssh -T git@github.com` 인증 성공 확인

### 2. perf 설치 확인
- `perf --version` → 미설치 확인 (`command not found`)
- 원인 추정: NVIDIA 커스텀 L4T 커널이라 표준 `linux-tools-$(uname -r)` 패키지가 존재하지 않을 가능성

### 3. perf 바이너리 확보 (핵심 성과)
- Tegra 커널(`6.8.12-1021-tegra`) 전용 `linux-tools` 패키지는 존재하지 않음 (NVIDIA 벤더 커널이라 Ubuntu 표준 저장소에 미등록)
- 우회: 표준 `linux-tools-6.8.0-138-generic` 패키지에 포함된 perf 바이너리(버전 6.8.12)를 커널 버전 무관하게 직접 실행 → **정상 동작 확인**
- `perf stat -- ls /` 결과: **하드웨어 PMU 이벤트(cycles, instructions, branches, branch-misses)가 전부 정상 수집됨**
  - VM(VirtualBox)에서는 PMU 가상화 미지원으로 소프트웨어 이벤트만 됐던 것과 대조적 — 실물 ARM 하드웨어의 이점을 확인
- alias 등록: `alias perf=/usr/lib/linux-tools/6.8.0-138-generic/perf`

### 4. CPU 부하 예제 + perf record + Flame Graph
- `src/hot.c`: 재귀 `fib(42)` (`-O0` 컴파일로 인라이닝 방지), 실행 시간 약 2.5초
- `sudo perf record -g -- src/hot` → 10,358 샘플 수집, call graph에서 `fib` 재귀 스택 확인
- `FlameGraph`(Brendan Gregg) 툴체인으로 `flame_fib42.svg` 생성
  - `perf script` → `stackcollapse-perf.pl` → `flamegraph.pl`
  - 외부 툴(`tools/FlameGraph/`)은 `.gitignore` 처리, 결과 SVG만 커밋

### 5. Flame Graph 결과 해석
- 모양: `main`/`hot` 받침대 위로 `fib` 박스가 약 24층 쌓인 "뾰족탑(stalagmite)" 형태
- 위로 갈수록 박스 폭이 점점 좁아짐 (100% → 1.52%) — `fib(n-1)+fib(n-2)` 재귀에서 좌측 가지가 더 깊이 파고들고 우측 가지가 상대적으로 얕게 끝나는 비대칭 재귀 구조가 폭 변화로 시각화됨
- x≈213 위치에 `[[kernel.kallsyms]]`가 여러 깊이(y=37~245)에 0.01%씩 반복 등장 — 특정 재귀 경로가 커널 트랩(타이머 인터럽트 추정)에 반복적으로 인터럽트된 흔적

### 6. perf stat 비교: I/O 짧은 프로세스 vs 연산 집중형 워크로드
| 지표 | `ls /` | `fib(42)` |
|---|---|---|
| task-clock | 1.53 msec | 2,582.44 msec |
| cycles | 1,612,215 | 3,856,861,365 |
| instructions | 1,468,134 | 13,004,930,685 |
| IPC | 0.91 | 3.37 |
| branches | 304,389 | 3,034,481,557 |
| branch-misses | 15,552 (5.11%) | 220,963 (0.01%) |

- `fib`처럼 분기 패턴이 규칙적인 연산 집중형 루프는 분기 예측기가 거의 완벽하게 학습되어 미스율이 0.01% 수준까지 떨어지고, IPC도 3.37까지 올라감 (ARM 슈퍼스칼라 파이프라인이 거의 스톨 없이 가동됨).
- `ls`는 초단명 프로세스라 동적 링킹/파일시스템 조회의 불규칙한 분기 + 예측기 워밍업 부족으로 미스율이 500배 가까이 높고 IPC도 1 미만.

### 7. ftrace 실습
- `available_tracers`에 `nop`만 존재 — Tegra 커널이 `function`/`function_graph` tracer를 컴파일에서 제외함 (벤더 커널 트레이싱 기능 제약, perf 이슈와 같은 맥락)
- 대안: `available_events`(1,877개) 중 `raw_syscalls:sys_enter/exit`, `sched:sched_switch` 이벤트 트레이싱으로 진행
- `fib(42)` 실행 중 syscall은 실행 초반(런타임 초기화) 0.42ms 구간에만 집중되고, 이후 연산 구간(2.5초)은 완전히 조용함 — 순수 유저모드 CPU 바운드 워크로드임을 커널 레벨에서 확인
- **중요 발견**: `perf stat`에서는 `context-switches:u 0`이었지만 ftrace에는 `sched_switch`(hot ↔ irq/189-aerdrv, hot ↔ kworker/0:2)가 다수 기록됨
  - 모순 아님: `perf stat`의 `:u` suffix는 user-space 이벤트만 카운트하므로 커널 인터럽트에 의한 선점은애초에 집계 대상이 아니었음
  - ftrace는 커널 레벨 전체를 잡아내므로 PCIe AER 인터럽트(`irq/189-aerdrv`)가 주기적으로 프로세스를 선점하는 게 드러남
  - 교훈: 도구별로 "무엇을 세는지(scope)"가 다르면 겉보기 모순이 발생할 수 있다는 실제 사례

### 8. 세 번째 워크로드: I/O 바운드 (`io_bound.c`, write() 200만 회 반복)
- `time` 결과: `real 2.058s`, `user 0.148s`, `sys 1.907s` — `fib`(user 2.581s/sys 0.000s)와 정반대의 시간 분포
- ~~`sudo perf stat` (커널 포함) 재측정 시 실행시간이 0.686s로 단축됨 — 캐시 워밍업 효과로 추정.~~
  - **⚠️ 이 결론은 이후 반복 측정으로 반증됨.** 0.686s는 캐시 효과가 아니라 **무효한 측정**이었다. `open()`이 EACCES로 실패했는데 당시 코드에 반환값 검사가 없어, `write(-1, ...)`을 200만 번 호출해 전부 EBADF로 실패하는 시간을 측정한 것. 동일 조건 5회 재측정 결과는 **2.0615 ± 0.0149s**. 전체 근거는 [`반복측정_결과.md`](반복측정_결과.md) 발견 2 참고.
- 커널 포함 `perf stat` (5회 반복, 수정 후): cycles 3,088,426,602 / instructions 5,911,112,075 / **IPC 1.91** / branch-miss 0.02%
- ftrace로 syscall 확인: `NR 64`(write)가 200만 번 중 14,727번만 캡처됨 — **ftrace 기본 circular 버퍼가 고빈도 이벤트로 오버플로**되어 마지막 ~22ms 구간만 남은 것. `fib`(135줄, 버퍼 여유)와 대조적. 버퍼 오버플로를 피하려면 `buffer_size_kb`를 늘려야 한다는 실전 교훈.

### 세 워크로드 최종 비교
`fib`·`io_bound` 열은 5회 반복 측정값으로 갱신했다 ([`반복측정_결과.md`](반복측정_결과.md)). `ls /`는 1회 측정값이다.

| | `ls /` (순간) | `fib(42)` (연산 지속) | `io_bound` (I/O 지속) |
|---|---|---|---|
| 실행시간 | 1.53ms | 2.578s ±0.5% | 2.053s ±1.1% |
| user/sys 비율 | 거의 user | 거의 전부 user | 대부분 sys |
| IPC | 0.91 | 3.37 | 1.91 (커널 포함) |
| branch-miss | 5.11% | 0.01% | 0.02% (커널 포함) |
| syscall 패턴 (ftrace) | 짧아서 전체 캡처됨 | 초반에만 집중, 이후 조용 | 버퍼 오버플로로 끝부분만 남음 |

### 9. 네 번째 워크로드: 멀티스레드 (`multithread.c`, 4 threads x fib(38))
- 6코어 장비에서 스레드 4개 동시 실행: `real 0.386s`, `user 1.520s` → 병렬 가속 약 3.94배 (이상적 4배에 근접)
- `sudo perf stat`: `context-switches: 45`, `cpu-migrations: 2`, `3.935 CPUs utilized`
  - ~~이번 실습 전체에서 처음으로 0이 아닌 값 확인.~~ **⚠️ 이 해석은 반복 측정으로 반증됨.** 단일 스레드 워크로드가 0으로 나온 것은 `sudo` 없이 측정해 `:u`(user-mode) 스코프였기 때문이며, 워크로드 특성이 아니라 측정 방식의 산물이었다. `sudo`로 통일해 재측정하면 `hot`도 context-switches 22 / cpu-migrations 2가 나온다. 멀티스레드의 실제 구별점은 **`CPUs utilized 3.905`**(상대편차 0.47%)뿐이다. [`반복측정_결과.md`](반복측정_결과.md) 발견 3 참고.
- `IPC 3.37`, `branch-miss 0.01%` — 단일 스레드 `fib(42)`와 거의 동일. 병렬화가 코어별 실행 효율은 유지하면서 처리량만 늘렸다는 근거.
- ftrace `sched_migrate_task`로 실제 마이그레이션 이벤트 확인. pid 필터링 없이 시스템 전체를 추적하니 상당수(rcu_preempt, kworker, sshd, python, NetworkManager, containerd 등)가 우리 프로세스와 무관한 배경 활동이었고, 진짜 `multithread` 자신의 마이그레이션은 3건 (`perf stat`의 `cpu-migrations: 2`와 대략 일치, 서로 다른 실행이라 완전 동일하지는 않음)
  - 교훈: 타겟 pid로 필터링하지 않고 시스템 전체를 트레이싱하면 노이즈에 신호가 묻힌다.
- 부가 관찰: CPU 부하가 오르자 `sugov:0`(schedutil 주파수 governor 커널 스레드)와 `multithread` 스레드가 매우 빈번하게 교차 스위칭됨 — 부하 증가에 따른 주파수 스케일링 개입이 스케줄러 레벨에서 관찰됨
