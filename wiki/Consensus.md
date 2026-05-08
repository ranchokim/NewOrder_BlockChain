# 합의 알고리즘

NewOrder의 합의 알고리즘은 라운드로빈 Proof of Authority입니다. 기존 Proof
of Work처럼 해시 퍼즐을 풀지 않고, 사전에 등록된 validator가 정해진 순서대로
블록을 생성하고 개인키로 서명합니다.

## 선택 이유

이 프로젝트는 공개 채굴망이 아니라 로컬/허가형 교육용 블록체인입니다. 이
조건에서는 PoW보다 PoA가 훨씬 효율적입니다.

- 블록 생성에 반복 해싱이 필요 없습니다.
- 에너지 비용이 거의 없습니다.
- 3개 노드 같은 소규모 네트워크에서 지연 시간이 낮습니다.
- validator 서명만 검증하면 되므로 블록 검증 비용이 작습니다.

## 블록 생성 절차

1. 모든 노드는 동일한 validator 주소 목록을 설정합니다.
2. 다음 블록 높이에서 담당 validator를 계산합니다.
3. 담당 validator 노드는 메모리풀 트랜잭션과 보상 트랜잭션을 묶습니다.
4. 블록 해시를 validator 개인키로 서명합니다.
5. 피어 노드는 담당 순서, validator 공개키, 블록 서명을 검증한 뒤 블록을 추가합니다.

## 담당 validator 계산

```text
validator_index = (block_height - 1) % validator_count
```

예를 들어 validator가 3개면 1번 블록은 첫 번째 validator, 2번 블록은 두 번째
validator, 3번 블록은 세 번째 validator가 생성합니다.

## 체인 선택

피어 동기화 시 더 긴 체인이 들어오면 다음 조건을 확인한 뒤 교체합니다.

- 모든 블록의 `previous_hash`가 이전 블록 해시와 일치해야 합니다.
- 각 블록의 validator가 해당 높이의 담당 validator여야 합니다.
- validator 공개키가 validator 주소와 일치해야 합니다.
- validator 서명이 유효해야 합니다.

## 제한 사항

현재 구현은 효율을 우선한 단순 PoA입니다. 실제 운영망에는 validator 선출,
키 교체, slashing, Byzantine fault tolerance, 네트워크 partition 복구 정책이
추가로 필요합니다.
