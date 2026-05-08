# AI 결제

NewOrder의 AI 결제 게이트웨이는 NO 코인 `transfer` 트랜잭션을 결제 수단으로 사용합니다.
별도의 스마트컨트랙트나 새 트랜잭션 유형 없이, 기존 송금 기능만으로 AI 서비스 이용 권한을
온체인에서 검증합니다.

## 결제 흐름

```
고객          노드(HTTP API)          블록체인
  |                  |                    |
  |-- ai-payment-create -->               |
  |<-- payment_id, memo, amount_due ---  |
  |                  |                    |
  |-- NO 송금 (memo=AIPAY:{id}) -------> |
  |                  |-- 블록 생성 -----> |
  |                  |                    |
  |-- ai-verify ----->                    |
  |<-- status: paid --------------------- |
  |                  |                    |
  |-- ai-consume ---->                    |
  |<-- 서비스 응답 ---------------------- |
```

## 결제 요청 구조

| 필드 | 설명 |
|------|------|
| `payment_id` | `AIP` 접두사의 고유 식별자 |
| `service_id` | 서비스 종류 (`chat-basic`, `summary` 등) |
| `customer_address` | 결제 주체의 NO 주소 |
| `merchant_address` | 수취인 NO 주소 (노드의 validator 지갑) |
| `units` | 구매하는 서비스 단위 수 |
| `amount_due` | 지불해야 할 NO 금액 (`price_per_unit × units`) |
| `memo` | 정산 트랜잭션에 포함해야 하는 메모 (`AIPAY:{payment_id}`) |
| `status` | `pending` / `paid` / `consumed` |
| `consumed_units` | 지금까지 소비한 단위 수 |

## 기본 제공 서비스

| service_id | 이름 | 단가 | 단위 |
|------------|------|------|------|
| `chat-basic` | Basic AI Chat | 0.25 NO | prompt |
| `summary` | Document Summary | 0.75 NO | document |

## 결제 검증 규칙

`verify` 호출 시 노드는 체인을 순회하여 다음 조건을 모두 만족하는 `transfer` 트랜잭션을 찾습니다.

- `sender == customer_address`
- `recipient == merchant_address`
- `memo == AIPAY:{payment_id}`
- `amount >= amount_due`

조건을 만족하는 트랜잭션이 확인 블록에 있으면 `status`를 `paid`로 변경합니다.
트랜잭션이 아직 블록에 포함되지 않은 경우 `status`는 `pending` 그대로 유지됩니다.

## merchant 주소 설정

노드 실행 시 `--validator`를 지정하면 validator 주소가 자동으로 merchant 주소가 됩니다.
명시적으로 다른 주소를 사용하려면 `--ai-merchant`를 추가합니다.

```bash
./neworder_node --validator NO... --validators NO1...,NO2...,NO3... --ai-merchant NO...
```

## 주의 사항

- 결제 데이터는 `data_dir/ai_payments.json`에 저장됩니다. 이 파일은 오프체인 데이터이므로
  노드를 교체하거나 `data_dir`을 삭제하면 결제 기록이 사라집니다.
- 같은 payment_id에 대해 `consume`을 `units`번 초과해 호출하면 오류가 반환됩니다.
- 현재 AI 응답은 결정론적 로컬 함수로 생성되며, 외부 AI 서비스와 연결되지 않습니다.
