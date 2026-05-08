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
