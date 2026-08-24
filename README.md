# jetson-perf-profiling

Jetson Orin(실물 ARM 하드웨어)에서 `perf`와 `ftrace`로 성능 프로파일링을 실습한 기록. 가상머신(VirtualBox)에서는 PMU 하드웨어 이벤트가 가상화 제약으로 막혀있었던 것을 계기로, 실물 장비에서 처음부터 다시 재현하며 벤더 커스텀 커널(NVIDIA L4T) 환경에서 부딪히는 실전 이슈들을 기록했다.

## 먼저 볼 것

**포트폴리오 페이지 (완성된 결과 요약)**: <https://leeyunhome.github.io/jetson-perf-profiling/>

## 이 저장소의 문서 구성

| 문서 | 용도 |
|---|---|
| [`docs/index.html`](docs/index.html) | 완성된 결과를 정리한 포트폴리오 페이지 (GitHub Pages로 배포됨) |
| [`docs/log.md`](docs/log.md) | 진행 순서대로 남긴 원본 작업 로그 |
| [`docs/시행착오_기록.md`](docs/시행착오_기록.md) | 진행 중 실제로 막혔던 문제와 해결 과정 |
| [`docs/쉽게_설명한_성능분석_실습.md`](docs/쉽게_설명한_성능분석_실습.md) | 전문용어 없이 비유로 풀어쓴 설명 |
| [`src/`](src/) | 실습에 쓴 예제 프로그램 3종 (hot.c=연산 지속형 fib, io_bound.c=I/O 지속형, multithread.c=병렬) |
| [`docs/flame_fib42.svg`](docs/flame_fib42.svg) | perf record + FlameGraph로 생성한 화염 그래프 |

## 환경

- Device: Jetson Orin (Tegra), 6 CPU cores
- Kernel: 6.8.12-1021-tegra (NVIDIA 커스텀 L4T 빌드)
- perf: 표준 linux-tools-generic 패키지의 바이너리를 커널 버전 무관하게 직접 실행 (자세한 이유는 시행착오 기록 참고)
- 코드 포맷: .clang-format 참고 (짧은 함수도 항상 멀티라인으로 펼침)

## 핵심 발견 요약

1. VM에서는 못 보던 하드웨어 PMU 이벤트(cycles/instructions/branches)가 실물 하드웨어에서는 전부 정상 수집됨
2. 워크로드 종류(순간형 ls / 연산 지속형 fib / I/O 지속형 / 병렬)에 따라 IPC, 분기미스율, 컨텍스트 스위치 패턴이 완전히 다르게 나타남
3. perf stat과 ftrace가 서로 다른 걸 셀 수 있다 (예: :u scope 차이) — 도구 하나만 믿으면 안 됨
4. 벤더 커스텀 커널(Tegra)에서는 perf/ftrace의 표준 기능(패키지, function_graph tracer)이 빠져있을 수 있고, 우회책이 필요함

## 재현 방법

perf 확보 (Tegra 커널엔 표준 패키지가 없음 — 대안 바이너리 사용):

    sudo apt install -y linux-tools-generic
    PERF=/usr/lib/linux-tools/6.8.0-138-generic/perf

예제 컴파일:

    gcc -O0 -g -o src/hot src/hot.c
    gcc -O0 -g -o src/io_bound src/io_bound.c
    gcc -O0 -g -pthread -o src/multithread src/multithread.c

프로파일링:

    sudo $PERF stat -- src/hot
    sudo $PERF record -g -o /tmp/perf.data -- src/hot

자세한 명령어와 결과는 docs/log.md 에 순서대로 정리돼 있다.
