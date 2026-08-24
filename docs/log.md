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
