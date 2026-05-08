# Explorer API

기본 주소는 노드 실행 시 지정한 HTTP 주소입니다. 기본값은
`http://127.0.0.1:8080`입니다.

## 체인 조회

- `GET /stats`: 코인, 높이, 최신 해시, PoA validator, 메모리풀, 토큰/컨트랙트 수
- `GET /chain`: 전체 블록 목록
- `GET /blocks/{height-or-hash}`: 특정 블록
- `GET /tx/{txid}`: 트랜잭션 검색
- `GET /mempool`: 대기 트랜잭션

## 주소 조회

- `GET /address/{address}`: NO 잔액과 토큰 잔액

## 토큰 조회

- `GET /tokens`: 발행된 토큰 목록
- `GET /tokens/{token_id}`: 특정 토큰 정보
- `GET /tokens/{token_id}/balances/{address}`: 특정 주소의 토큰 잔액

## 컨트랙트 조회

- `GET /contracts`: 배포된 컨트랙트 목록
- `GET /contracts/{contract_id}`: 컨트랙트 메타데이터와 현재 상태

## AI 결제

- `GET /ai/services`: 이용 가능한 AI 서비스 목록
- `GET /ai/payments/{payment_id}`: 결제 요청 상태 조회
- `POST /ai/payments`: 결제 요청 생성. 요청 본문: `{"service_id": "chat-basic", "customer_address": "NO...", "units": 1}`
- `POST /ai/payments/{payment_id}/verify`: 결제 트랜잭션이 블록에 포함되었는지 확인하고 상태를 `paid`로 갱신
- `POST /ai/payments/{payment_id}/consume`: 서비스 단위 1개 소비. 요청 본문: `{"prompt": "내용"}`

결제 상태 값: `pending` → `paid` → `consumed`

## 노드 작업

- `GET /mine?address={NO...}`: 현재 담당 validator 노드가 PoA 블록을 생성하고 보상을 지정 주소로 지급
- `GET /peers`: 연결된 피어 목록
- `POST /transactions`: 서명된 트랜잭션 제출
- `POST /peers`: 피어 연결. 요청 본문: `{"peer": "127.0.0.1:9334"}`
