# 사용법

## validator 지갑 생성

```powershell
python -m neworder.wallet create --wallet validator1.json
python -m neworder.wallet create --wallet validator2.json
python -m neworder.wallet create --wallet validator3.json
```

세 지갑에서 생성된 `NO...` 주소를 같은 순서로 모든 노드의 `--validators`에
전달합니다.

## validator 노드 실행

```powershell
python -m neworder --host 127.0.0.1 --port 8080 --p2p-port 9333 --data-dir data/node1 --validator-wallet validator1.json --validators NO1...,NO2...,NO3...
```

익스플로러 API는 `http://127.0.0.1:8080`에서 열립니다.

## PoA 블록 생성

```powershell
python -m neworder.wallet --wallet validator1.json mine
```

명령 이름은 기존 호환성을 위해 `mine`으로 유지하지만, 실제 동작은 Proof of
Authority 블록 생성입니다. 현재 높이의 담당 validator 노드만 다음 블록을 만들
수 있습니다.

## 잔액 조회

```powershell
python -m neworder.wallet --wallet validator1.json balance
```

응답에는 네이티브 NO 잔액과 보유 토큰 잔액이 함께 표시됩니다.

## NO 전송

```powershell
python -m neworder.wallet --wallet validator1.json send --to NO... --amount 10 --fee 0.01
```

트랜잭션은 먼저 메모리풀에 들어가며, 다음 PoA 블록에 포함됩니다.

## 토큰 발행 및 전송

```powershell
python -m neworder.wallet --wallet wallet1.json token-create --name "Example Token" --symbol EXT --supply 1000000 --decimals 2
python -m neworder.wallet --wallet wallet1.json mine
```

PoA 블록 생성 후 `GET /tokens`에서 `NOT...` 형식의 `token_id`를 확인합니다.

```powershell
python -m neworder.wallet --wallet wallet1.json token-send --token-id NOT... --to NO... --amount 100
python -m neworder.wallet --wallet wallet1.json mine
```

## AI 결제 흐름

AI 결제는 NO 코인 `transfer` 트랜잭션으로 정산됩니다.

**1. 이용 가능한 서비스 목록 확인**

```powershell
python -m neworder.wallet ai-services
```

**2. 결제 요청 생성**

```powershell
python -m neworder.wallet --wallet wallet1.json ai-payment-create --service-id chat-basic --units 2
```

응답에서 `payment_id` (`AIP...`)와 `merchant_address`, `amount_due`, `memo`를 확인합니다.

**3. NO 코인으로 결제**

```powershell
python -m neworder.wallet --wallet wallet1.json ai-pay --payment-id AIP...
python -m neworder.wallet --wallet validator1.json mine
```

`ai-pay`는 결제 요청에 명시된 `merchant_address`에 `amount_due`를 `memo`와 함께 전송합니다.
트랜잭션이 PoA 블록에 포함되어야 결제가 유효합니다.

**4. 결제 검증**

```powershell
python -m neworder.wallet ai-verify --payment-id AIP...
```

응답의 `status`가 `paid`이면 결제가 온체인에서 확인된 것입니다.

**5. 서비스 사용**

```powershell
python -m neworder.wallet ai-consume --payment-id AIP... --prompt "요청 내용"
```

구매한 `units` 수만큼 호출할 수 있습니다. 모든 단위를 소비하면 `status`가 `consumed`로
변경되고 추가 호출은 거절됩니다.

## 3개 노드 연결

```powershell
python -m neworder --port 8080 --p2p-port 9333 --data-dir data/node1 --validator-wallet validator1.json --validators NO1...,NO2...,NO3...
python -m neworder --port 8081 --p2p-port 9334 --data-dir data/node2 --validator-wallet validator2.json --validators NO1...,NO2...,NO3...
python -m neworder --port 8082 --p2p-port 9335 --data-dir data/node3 --validator-wallet validator3.json --validators NO1...,NO2...,NO3...
```

```powershell
Invoke-RestMethod http://127.0.0.1:8081/peers -Method Post -ContentType "application/json" -Body '{"peer":"127.0.0.1:9333"}'
Invoke-RestMethod http://127.0.0.1:8082/peers -Method Post -ContentType "application/json" -Body '{"peer":"127.0.0.1:9333"}'
```
