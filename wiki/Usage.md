# 사용법

## 빌드

```bash
cd src
make
```

세 개의 바이너리가 생성됩니다: `neworder_c` (스모크 테스트/벤치마크), `neworder_node` (HTTP 노드), `neworder_wallet` (지갑 CLI).

## validator 지갑 생성

```bash
./neworder_wallet create --wallet validator1.json
./neworder_wallet create --wallet validator2.json
./neworder_wallet create --wallet validator3.json
```

세 지갑에서 생성된 `NO...` 주소를 같은 순서로 모든 노드의 `--validators`에
전달합니다.

## validator 노드 실행

```bash
./neworder_node --port 8080 --data-dir data/node1 --validator NO... --validators NO1...,NO2...,NO3...
```

익스플로러 API는 `http://127.0.0.1:8080`에서 열립니다.

## PoA 블록 생성

```bash
./neworder_wallet mine --wallet validator1.json
```

현재 높이의 담당 validator 노드만 다음 블록을 만들 수 있습니다.

## 잔액 조회

```bash
./neworder_wallet balance --wallet wallet1.json
```

응답에는 네이티브 NO 잔액과 보유 토큰 잔액이 함께 표시됩니다.

## NO 전송

```bash
./neworder_wallet send --wallet wallet1.json --to NO... --amount 10 --fee 0.01
```

트랜잭션은 먼저 메모리풀에 들어가며, 다음 PoA 블록에 포함됩니다.

## 토큰 발행 및 전송

```bash
./neworder_wallet token-create --wallet wallet1.json --name "Example Token" --symbol EXT --supply 1000000
./neworder_wallet mine --wallet wallet1.json
```

PoA 블록 생성 후 `GET /tokens`에서 `NOT...` 형식의 `token_id`를 확인합니다.

```bash
./neworder_wallet token-send --wallet wallet1.json --token NOT... --to NO... --amount 100
./neworder_wallet mine --wallet wallet1.json
```

## AI 결제 흐름

AI 결제는 NO 코인 `transfer` 트랜잭션으로 정산됩니다.

**1. 이용 가능한 서비스 목록 확인**

```bash
./neworder_wallet ai-services
```

**2. 결제 요청 생성**

```bash
./neworder_wallet ai-payment-create --wallet wallet1.json --service-id chat-basic --units 2
```

응답에서 `payment_id` (`AIP...`)와 `merchant_address`, `amount_due`, `memo`를 확인합니다.

**3. NO 코인으로 결제**

```bash
./neworder_wallet ai-pay --wallet wallet1.json --payment-id AIP...
./neworder_wallet mine --wallet validator1.json
```

`ai-pay`는 결제 요청에 명시된 `merchant_address`에 `amount_due`를 `memo`와 함께 전송합니다.
트랜잭션이 PoA 블록에 포함되어야 결제가 유효합니다.

**4. 결제 검증**

```bash
./neworder_wallet ai-verify --payment-id AIP...
```

응답의 `status`가 `paid`이면 결제가 온체인에서 확인된 것입니다.

**5. 서비스 사용**

```bash
./neworder_wallet ai-consume --wallet wallet1.json --payment-id AIP... --prompt "요청 내용"
```

구매한 `units` 수만큼 호출할 수 있습니다. 모든 단위를 소비하면 `status`가 `consumed`로
변경되고 추가 호출은 거절됩니다.

## 3개 노드 연결

```bash
./neworder_node --port 8080 --data-dir data/node1 --validator NO1... --validators NO1...,NO2...,NO3...
./neworder_node --port 8081 --data-dir data/node2 --validator NO2... --validators NO1...,NO2...,NO3...
./neworder_node --port 8082 --data-dir data/node3 --validator NO3... --validators NO1...,NO2...,NO3...
```
