# 스마트컨트랙트

NewOrder의 스마트컨트랙트는 보안상 임의 코드를 실행하지 않습니다. 대신 노드가
동일하게 재현할 수 있는 내장 컨트랙트 유형을 온체인 트랜잭션으로 처리합니다.

## 지원 컨트랙트

### counter

정수 카운터 상태를 관리합니다.

- `increment`: 값을 증가시킵니다. `{"amount": 1}` 형식의 인자를 받습니다.
- `decrement`: 값을 감소시킵니다.
- `reset`: 값을 0으로 초기화합니다.

### key_value

문자열 키에 JSON 값을 저장합니다.

- `set`: `{"key": "name", "value": "alice"}` 값을 저장합니다.
- `delete`: `{"key": "name"}` 값을 삭제합니다.

## 컨트랙트 배포

```bash
./neworder_wallet contract-deploy --wallet wallet1.json --name VoteCounter --type counter
./neworder_wallet mine --wallet wallet1.json
```

PoA 블록 생성 후 `GET /contracts`에서 `NOC...` 형식의 `contract_id`를 확인합니다.

## 컨트랙트 호출

```bash
./neworder_wallet contract-call --wallet wallet1.json --contract NOC... --method increment --amount 3
./neworder_wallet mine --wallet wallet1.json
```

## 상태 조회

```bash
curl http://127.0.0.1:8080/contracts/NOC...
```

컨트랙트 상태는 체인의 deploy/call 트랜잭션을 순서대로 재생하여 계산됩니다.
