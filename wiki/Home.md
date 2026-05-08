# NewOrder Wiki

NewOrder는 학습과 로컬 실험을 위한 표준 라이브러리 기반 블록체인입니다.
네이티브 코인은 **NewOrder**, 심볼은 **NO**입니다.

## 구성 요소

- 네이티브 코인: 블록 보상, 잔액, 수수료를 처리합니다.
- P2P 노드: TCP JSON-line 메시지로 블록과 트랜잭션을 전파합니다.
- 지갑: 주소 생성, 송금, PoA 블록 생성 요청, 토큰 발행, 스마트컨트랙트 호출을 수행합니다.
- 익스플로러 API: 체인, 블록, 트랜잭션, 주소, 토큰, 컨트랙트 상태를 조회합니다.
- 스마트컨트랙트: 내장형 `counter`, `key_value` 컨트랙트를 온체인 트랜잭션으로 실행합니다.
- 토큰: `token_create`, `token_transfer` 트랜잭션으로 사용자 발행 토큰을 관리합니다.

## 문서 목록

- [블록체인 설명](Overview.md)
- [합의 알고리즘](Consensus.md)
- [사용법](Usage.md)
- [스마트컨트랙트](SmartContracts.md)
- [토큰 발행](Tokens.md)
- [Explorer API](ExplorerAPI.md)

## 주의

NewOrder는 프로덕션 암호화폐가 아닙니다. 합의, 네트워크 보안, 지갑 암호화,
스마트컨트랙트 VM 샌드박싱이 단순화되어 있으므로 실제 자산 운용에는 사용할 수 없습니다.
