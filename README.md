# SooNote

텍스트 파일을 Firebase와 자동으로 동기화하는 macOS 트레이 앱입니다.

---

## 주요 기능

- 📁 지정 폴더의 텍스트 파일 자동 감시 및 동기화
- ☁️ Firebase Storage + Firestore 기반 클라우드 저장
- 🔄 자동 Push (파일 변경 감지 후 10초 디바운스)
- 📥 자동 Pull (5분마다 + 시작 시)
- 🗜️ 파일 압축 전송 (gzip)
- 🔐 설정 파일 암호화 (API 키 보호)
- 🌏 한글/특수문자/긴 파일명 지원
- 🔁 네트워크 오류 시 자동 재시도 (최대 3회)
- 🚫 중복 실행 방지

---

## 지원 파일 형식

```
txt, md, log, csv, json, xml,
html, css, js, cpp, h, py,
ts, jsx, tsx, vue, swift, kt
```

---

## 설치 방법

### 요구사항

- macOS 10.15 이상
- Qt 5.15 이상
- Firebase 프로젝트 (Blaze 플랜 또는 무료)

### 빌드

```bash
git clone https://github.com/shong1332/soonote.git
cd soonote
qmake soonote.pro
make
```

또는 Qt Creator에서 `soonote.pro` 열고 빌드

### 실행

```bash
open build/Desktop-Release/soonote.app
```

---

## Firebase 설정 방법

### 1. Firebase 프로젝트 생성

1. [Firebase 콘솔](https://console.firebase.google.com) 접속
2. **프로젝트 추가** 클릭
3. 프로젝트 이름 입력 후 생성

### 2. Firestore Database 활성화

1. 왼쪽 메뉴 → **Firestore Database**
2. **데이터베이스 만들기** 클릭
3. **테스트 모드** 선택
4. 리전: `asia-northeast3 (서울)` 권장

### 3. Firebase Storage 활성화

1. 왼쪽 메뉴 → **Storage**
2. **시작하기** 클릭 (Blaze 플랜 필요)
3. 보안 규칙을 아래와 같이 설정:

```
rules_version = '2';
service firebase.storage {
  match /b/{bucket}/o {
    match /{allPaths=**} {
      allow read, write: if true;
    }
  }
}
```

### 4. API 키 확인

1. **프로젝트 설정** (⚙️ 아이콘) → **일반** 탭
2. **웹 API 키** 복사
3. **프로젝트 ID** 복사

---

## 사용법

### 최초 설정

1. 앱 실행 → 트레이 아이콘 확인
2. 트레이 아이콘 클릭 → **설정**
3. 아래 정보 입력:
   - **동기화 폴더**: 감시할 로컬 폴더 경로
   - **Firebase API 키**: 위에서 복사한 웹 API 키
   - **Firebase 프로젝트 ID**: 위에서 복사한 프로젝트 ID
4. **확인** 클릭

### 트레이 메뉴

| 메뉴 | 설명 |
|------|------|
| 설정 | Firebase 및 폴더 설정 변경 |
| 전체 업로드 | 폴더 내 모든 파일 강제 업로드 |
| 전체 다운로드 | Firebase의 모든 파일 강제 다운로드 |
| 종료 | 앱 종료 |

### 자동 동기화

- 파일 **생성/수정** → 10초 후 자동 업로드
- 파일 **삭제** → 즉시 Firebase에서도 삭제
- **5분마다** 자동으로 서버 변경사항 Pull

### 주의사항

- 50MB 이상 파일은 동기화 제외
- 숨김 파일 (`.`으로 시작) 제외
- 심볼릭 링크 제외
- Mac/Windows 간 대소문자 충돌 주의
- Firestore 규칙 만료일(기본 30일) 이전에 보안 규칙 업데이트 권장

---

## 설정 파일 위치

```
~/.syncnote/
├── config.json      # 암호화된 설정 (API 키 등)
└── metadata.db      # 로컬 파일 해시 DB (SQLite)
```

---

## 라이선스

MIT License
