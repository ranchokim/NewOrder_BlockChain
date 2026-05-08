# 토큰 발행

NewOrder는 네이티브 코인 NO 위에서 사용자 정의 토큰을 발행할 수 있습니다.

## 토큰 발행

```powershell
python -m neworder.wallet --wallet wallet1.json token-create --name "Example Token" --symbol EXT --supply 1000000 --decimals 2
python -m neworder.wallet --wallet wallet1.json mine
```

PoA 블록 생성 후 `GET /tokens`에서 `NOT...` 형식의 `token_id`를 확인합니다. 발행자는
초기 공급량 전체를 보유합니다.

## 토큰 전송

```powershell
python -m neworder.wallet --wallet wallet1.json token-send --token-id NOT... --to NO... --amount 100
python -m neworder.wallet --wallet wallet1.json mine
```

## 토큰 잔액 조회

```powershell
Invoke-RestMethod http://127.0.0.1:8080/tokens/NOT.../balances/NO...
```

주소 조회 API인 `GET /address/{address}`도 보유 토큰 목록을 함께 반환합니다.

## 검증 규칙

- 토큰 심볼은 1~12자의 영문/숫자여야 합니다.
- 토큰 심볼은 네이티브 코인 심볼 `NO`와 같을 수 없습니다.
- 총 공급량은 0보다 커야 합니다.
- 소수점 자릿수는 0~18 사이여야 합니다.
- 토큰 전송자는 충분한 토큰 잔액을 보유해야 합니다.
