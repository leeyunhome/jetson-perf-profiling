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
