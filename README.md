# jetson-perf-profiling

Jetson Orin(실물 ARM 하드웨어)에서 `perf`와 `ftrace`로 CPU 프로파일링을 실습한 기록. 가상머신(VirtualBox)에서는 PMU 하드웨어 이벤트가 가상화 제약으로 막혀 있던 것을 계기로, 실물 장비에서 처음부터 다시 재현하며 벤더 커스텀 커널(NVIDIA L4T) 환경에서 부딪히는 실전 이슈들을 기록했다.

## 이 저장소의 범위 (먼저 읽어주세요)

이 저장소는 **`perf`/`ftrace` 기반 CPU 프로파일링 실습 한 건**으로 범위가 한정되어 있다. 짧은 단일 세션의 학습 기록이며, 임베디드 리눅스 실무 역량 전반을 대표하지 않는다.

- **여기서 다루는 것**: `perf stat`/`perf record`, 하드웨어 PMU 카운터 해석(IPC·분기 예측), Flame Graph 생성, ftrace tracepoint 이벤트 트레이싱, 워크로드 유형별 프로파일 비교
- **여기서 다루지 않는 것**: DMA-BUF / V4L2 zero-copy 파이프라인, Device Tree, out-of-tree 벤더 드라이버 통합, 부팅 시퀀스 최적화 — 이들은 이 저장소가 아니라 **세연테크에서의 실무 경험**에서 다룬 영역이다.

즉 이 저장소는 "프로파일링 도구를 실물 장비에서 직접 붙잡고 씨름한 기록"으로 읽어주시면 된다.

**부록**: Jetson에서 못 써본 `function_graph` tracer를 표준 커널의 별도 x86 서버에서 재현해본 대조 실험이 [`docs/x86_function_graph_비교.md`](docs/x86_function_graph_비교.md)에 있다. 본 실험(Jetson)과는 별개 환경/별개 세션이며, 이 부록에 한해서만 x86 데이터가 등장한다.

## 실습 세션 정보

- **일시**: 2026-08-24 하루, 17:27 ~ 22:44 KST (**약 5시간 20분**, 단일 세션)
- 커밋 22개가 모두 이 시간 범위 안에 있다. 여러 날에 걸친 장기 프로젝트가 아니다.
- 초기 커밋의 author는 `manager <mszeta@naver.com>` — **Jetson 기기의 로컬 계정(`manager@localhost`)에서 직접 커밋**했기 때문이다. 이후 정리 작업은 작업 PC에서 `leeyunhome`으로 커밋했다.

## 먼저 볼 것

**포트폴리오 페이지 (완성된 결과 요약)**: <https://leeyunhome.github.io/jetson-perf-profiling/>

## 이 저장소의 문서 구성

| 문서 | 용도 |
|---|---|
| [`docs/index.html`](docs/index.html) | 완성된 결과를 정리한 포트폴리오 페이지 (GitHub Pages로 배포됨) |
| [`docs/log.md`](docs/log.md) | 진행 순서대로 남긴 원본 작업 로그 |
| [`docs/반복측정_결과.md`](docs/반복측정_결과.md) | 각 워크로드 5회 반복 측정 결과 (평균·표준편차·range) |
| [`docs/시행착오_기록.md`](docs/시행착오_기록.md) | 진행 중 실제로 막혔던 문제와 해결 과정 |
| [`docs/ftrace_로그판독_인터럽트컨텍스트.md`](docs/ftrace_로그판독_인터럽트컨텍스트.md) | 이미 캡처한 로그의 **플래그 열**을 판독해 인터럽트 컨텍스트·선점·threaded IRQ를 규명 (재측정 없이) |
| [`docs/쉽게_설명한_성능분석_실습.md`](docs/쉽게_설명한_성능분석_실습.md) | 전문용어 없이 비유로 풀어쓴 설명 |
| [`src/`](src/) | 예제 프로그램 3종 + Makefile (`hot.c`=연산 지속형, `io_bound.c`=I/O 지속형, `multithread.c`=병렬) |
| [`scripts/bench.sh`](scripts/bench.sh) | 반복 측정 스크립트 (위 반복측정 결과를 생성) |
| [`notebooks/review.ipynb`](notebooks/review.ipynb) | 전체 세션을 처음부터 다시 실행하며 복습하는 노트북 (Jetson의 `mlops-lab` Jupyter 커널 기준) |
| [`docs/flame_fib42.svg`](docs/flame_fib42.svg) | perf record + FlameGraph로 생성한 화염 그래프 |
| [`docs/perf_annotate_fib.txt`](docs/perf_annotate_fib.txt) | `perf annotate`로 뽑은 `fib` 소스라인·어셈블리 단위 분석 원본 |
| [`docs/x86_function_graph_비교.md`](docs/x86_function_graph_비교.md) | (부록) 별도 x86 서버에서 `function_graph` tracer를 재현한 대조 실험 |
| [`docs/x86_function_graph_write.txt`](docs/x86_function_graph_write.txt) | 위 부록의 원본 트레이스 발췌 |
| [`docs/라즈베리파이_function_graph_비교.md`](docs/라즈베리파이_function_graph_비교.md) | (부록2) Raspberry Pi(ARM64, 표준 커널)에서 `function_graph`를 재현한 두 번째 대조 실험 |

## 측정 환경

아래 수치와 명령어는 **이 환경에서만 그대로 유효하다.** 특히 perf 바이너리 경로는 이 기기에 설치된 패키지 버전에 종속적이다.

| 항목 | 값 |
|---|---|
| Device | Jetson Orin (Tegra), 6 CPU cores |
| Kernel | `6.8.12-1021-tegra` (NVIDIA 커스텀 L4T 빌드) |
| Userspace | Ubuntu 24.04 (aarch64) |
| perf | `linux-tools-6.8.0-138-generic` 패키지의 바이너리 (버전 6.8.12) |
| 코드 포맷 | [`.clang-format`](.clang-format) — 짧은 함수도 항상 멀티라인으로 펼침 |

### perf 경로가 하드코딩이 아니라 변수인 이유

Tegra 커널에는 대응하는 `linux-tools-6.8.12-1021-tegra` 패키지가 **존재하지 않는다.** 그래서 `perf` 래퍼(`/usr/bin/perf`)는 커널 버전을 보고 "패키지를 설치하라"며 실패한다. 우회책으로 **버전이 다른 표준 패키지의 perf 바이너리를 절대경로로 직접 호출**한다 (자세한 경위는 [`docs/시행착오_기록.md`](docs/시행착오_기록.md) 3번 참고).

따라서 이 경로는 **환경마다 다르다.** 아래처럼 먼저 실제 경로를 찾아 변수에 담고 쓴다:

```sh
# 이 기기에 설치된 perf 바이너리를 찾는다
ls /usr/lib/linux-tools/*/perf

# 찾은 경로를 변수에 담는다 (이 저장소의 결과는 아래 경로로 측정됨)
export PERF=/usr/lib/linux-tools/6.8.0-138-generic/perf
"$PERF" --version   # -> perf version 6.8.12
```

`sudo`는 alias/함수를 무시하므로 `sudo perf ...`가 아니라 `sudo "$PERF" ...` 형태로 호출해야 한다.

그 외 환경 종속적인 값들:

| 값 | 위치 | 비고 |
|---|---|---|
| `/usr/lib/linux-tools/6.8.0-138-generic/perf` | `scripts/bench.sh`의 `PERF` 기본값 | `PERF=... bash scripts/bench.sh`로 덮어쓸 수 있음 |
| `/tmp/io_bound_test.<uid>.bin` | `src/io_bound.c`의 기본 출력 경로 | uid별로 분리됨 (같은 파일을 유저 권한과 `sudo`로 번갈아 열면 `fs.protected_regular`에 막힘 — [`docs/시행착오_기록.md`](docs/시행착오_기록.md) 8번 참고). `IO_BOUND_OUT` 환경변수로 덮어쓸 수 있음. 실행마다 새로 만들며 약 128MB 사용 |
| `/sys/kernel/debug/tracing` | ftrace 실습 전반 | tracefs 마운트 위치. 읽기에도 `sudo` 필요 |

## 재현 방법

```sh
# 1) perf 확보 (Tegra 커널엔 대응 패키지가 없음 -- 위 설명 참고)
sudo apt install -y linux-tools-common linux-tools-generic
export PERF=$(ls /usr/lib/linux-tools/*/perf | head -1)

# 2) 워크로드 빌드
make -C src            # hot, io_bound, multithread
make -C src clean      # 정리

# 3) 단발 프로파일링
sudo "$PERF" stat -- src/hot
sudo "$PERF" record -g -o /tmp/perf.data -- src/hot

# 4) 반복 측정 (5회 기본, 평균/표준편차/range 출력)
bash scripts/bench.sh 5
```

자세한 명령어와 결과는 [`docs/log.md`](docs/log.md)에 순서대로 정리돼 있다.

## 핵심 발견 요약

1. VM에서는 못 보던 하드웨어 PMU 이벤트(cycles/instructions/branches)가 실물 하드웨어에서는 전부 정상 수집됨
2. 워크로드 종류(순간형 `ls` / 연산 지속형 `fib` / I/O 지속형 / 병렬)에 따라 IPC, 분기미스율, 컨텍스트 스위치 패턴이 완전히 다르게 나타남
3. `perf stat`과 `ftrace`가 서로 다른 것을 셀 수 있다 (예: `:u` scope 차이) — 도구 하나만 믿으면 안 됨
4. 벤더 커스텀 커널(Tegra)에서는 `perf`/`ftrace`의 표준 기능(대응 패키지, `function_graph` tracer)이 빠져 있을 수 있고, 우회책이 필요함
5. **반복 측정이 위 실습의 결론 두 개를 반증했다** — 그중 하나는 "에러 체크를 빠뜨린 벤치마크가 크래시 대신 그럴듯한 거짓 숫자를 내놓고, 그게 잘못된 인사이트로 문서에 기록된" 사례였다 ([`docs/반복측정_결과.md`](docs/반복측정_결과.md))
6. 지표마다 신뢰도가 다르다 — instructions/branches는 상대편차 ±0.00%로 1회 측정도 인용 가능하지만, `cpu-migrations`는 ±37~60%로 단발 측정값으로는 어떤 주장도 할 수 없다
7. `perf annotate`는 재귀 호출처럼 같은 코드를 반복 실행하는 경우 "어느 호출 경로가 더 뜨거운가"를 답하지 못한다 — 그건 Flame Graph(호출 구조)의 역할이고, annotate는 명령어 단위 핫맵이라 둘은 서로 다른 질문에 답한다. 게다가 ARM PMU의 샘플 스큐 때문에 명령어별 퍼센트를 액면 그대로 믿으면 안 된다 ([`docs/log.md`](docs/log.md) 10절)
8. **도구가 이미 출력한 정보를 못 읽어서 놓친 게 있었다** — ftrace 매 줄 앞의 5글자 플래그 열이 인터럽트 컨텍스트(hardirq/softirq)·선점 깊이·재스케줄 대기 상태를 그대로 알려주는데, 처음엔 그냥 지나쳤다. 재측정 없이 같은 로그를 다시 읽는 것만으로 `irq/189-aerdrv` 선점 원인(RT 우선순위 50 threaded IRQ)과 top half/bottom half 2단계 처리 증거를 찾아냈다 ([`docs/ftrace_로그판독_인터럽트컨텍스트.md`](docs/ftrace_로그판독_인터럽트컨텍스트.md))
9. (부록) `function_graph`는 유저스페이스 함수가 아니라 커널 함수만 추적한다 — Tegra에 있었어도 `fib()`는 못 봤을 것이다. 표준 커널(x86)에서 재현하니 `write()`가 AppArmor 검사·ext4 inode 락·dirty page 스로틀링을 거치는 실제 커널 함수 트리를 볼 수 있었고, pid 필터링 없는 노이즈 문제(원격 데스크톱 데몬·로그 데몬 등)도 완전히 다른 환경에서 독립적으로 재현됐다 ([`docs/x86_function_graph_비교.md`](docs/x86_function_graph_비교.md))
