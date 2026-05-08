const pages = {
  overview: {
    title: "개요",
    html: `
      <p>NewOrder는 학습과 로컬 실험을 위한 C 언어 기반 블록체인입니다. 네이티브 코인은 <strong>NewOrder</strong>, 심볼은 <strong>NO</strong>입니다.</p>
      <div class="grid">
        <div class="info"><strong>코인</strong>NewOrder · NO · 최대 공급량 21,000,000</div>
        <div class="info"><strong>노드</strong>HTTP Explorer API를 제공하는 단일 바이너리 노드입니다.</div>
        <div class="info"><strong>스마트컨트랙트</strong>counter, key_value 내장 컨트랙트를 온체인 트랜잭션으로 실행합니다.</div>
        <div class="info"><strong>토큰</strong>token_create, token_transfer 트랜잭션으로 사용자 토큰을 발행하고 전송합니다.</div>
      </div>
      <h2>구성 요소</h2>
      <ul>
        <li>네이티브 코인: 블록 보상, 잔액, 수수료를 처리합니다.</li>
        <li>지갑: 주소 생성, 송금, PoA 블록 생성 요청, 토큰 발행, 스마트컨트랙트 호출을 수행합니다.</li>
        <li>익스플로러 API: 체인, 블록, 트랜잭션, 주소, 토큰, 컨트랙트 상태를 조회합니다.</li>
      </ul>
      <h2>트랜잭션 유형</h2>
      <ul>
        <li><code>transfer</code>: NO 네이티브 코인 전송</li>
        <li><code>token_create</code>: 사용자 정의 토큰 발행</li>
        <li><code>token_transfer</code>: 발행된 토큰 전송</li>
        <li><code>contract_deploy</code>: 내장 스마트컨트랙트 배포</li>
        <li><code>contract_call</code>: 배포된 스마트컨트랙트 호출</li>
      </ul>
      <p class="note">NewOrder는 교육용 구현입니다. 실제 자산 운용에는 감사된 암호화, 네트워크 보안, 지갑 암호화, 더 엄격한 합의 규칙이 필요합니다.</p>
    `,
  },
  consensus: {
    title: "합의 알고리즘",
    html: `
      <p>NewOrder의 합의 알고리즘은 라운드로빈 Proof of Authority입니다. 기존 Proof of Work처럼 해시 퍼즐을 풀지 않고, 사전에 등록된 validator가 정해진 순서대로 블록을 생성합니다.</p>
      <h2>선택 이유</h2>
      <ul>
        <li>블록 생성에 반복 해싱이 필요 없습니다.</li>
        <li>에너지 비용이 거의 없습니다.</li>
        <li>3개 노드 같은 소규모 네트워크에서 지연 시간이 낮습니다.</li>
        <li>validator 순서만 검증하면 되므로 블록 검증 비용이 작습니다.</li>
      </ul>
      <h2>블록 생성 절차</h2>
      <ol>
        <li>모든 노드는 동일한 validator 주소 목록을 설정합니다.</li>
        <li>다음 블록 높이에서 담당 validator를 계산합니다.</li>
        <li>담당 validator 노드는 메모리풀 트랜잭션과 보상 트랜잭션을 묶습니다.</li>
        <li>블록 해시를 SHA-256으로 계산합니다.</li>
        <li>피어 노드는 담당 순서와 블록 해시를 검증한 뒤 블록을 추가합니다.</li>
      </ol>
      <h2>담당 validator 계산</h2>
      ${code('validator_index = (block_height - 1) % validator_count')}
      <h2>체인 선택</h2>
      <ul>
        <li>피어 동기화 시 기존 체인보다 긴 체인만 후보가 됩니다.</li>
        <li>모든 블록의 <code>previous_hash</code>가 이전 블록 해시와 일치해야 합니다.</li>
        <li>각 블록의 validator가 해당 높이의 담당 validator여야 합니다.</li>
        <li>coinbase 금액이 블록 보상 + 수수료를 초과하지 않아야 합니다.</li>
      </ul>
      <p class="note">현재 구현은 효율을 우선한 단순 PoA입니다. 실제 운영망에는 validator 선출, 키 교체, slashing, Byzantine fault tolerance가 추가로 필요합니다.</p>
    `,
  },
  usage: {
    title: "사용법",
    html: `
      <h2>빌드</h2>
      ${code('cd src\nmake')}
      <h2>validator 지갑 생성</h2>
      ${code('./neworder_wallet create --wallet validator1.json\n./neworder_wallet create --wallet validator2.json\n./neworder_wallet create --wallet validator3.json')}
      <h2>validator 노드 실행</h2>
      ${code('./neworder_node --port 8080 --data-dir data/node1 --validator NO... --validators NO1...,NO2...,NO3...')}
      <h2>PoA 블록 생성</h2>
      ${code('./neworder_wallet mine --wallet validator1.json')}
      <p>현재 높이의 담당 validator 노드만 다음 블록을 만들 수 있습니다.</p>
      <h2>잔액 조회</h2>
      ${code('./neworder_wallet balance --wallet wallet1.json')}
      <p>응답에는 네이티브 NO 잔액과 보유 토큰 잔액이 함께 표시됩니다.</p>
      <h2>NO 전송</h2>
      ${code('./neworder_wallet send --wallet wallet1.json --to NO... --amount 10 --fee 0.01')}
      <h2>3개 노드 연결</h2>
      ${code('./neworder_node --port 8080 --data-dir data/node1 --validator NO1... --validators NO1...,NO2...,NO3...\n./neworder_node --port 8081 --data-dir data/node2 --validator NO2... --validators NO1...,NO2...,NO3...\n./neworder_node --port 8082 --data-dir data/node3 --validator NO3... --validators NO1...,NO2...,NO3...')}
    `,
  },
  contracts: {
    title: "스마트컨트랙트",
    html: `
      <p>NewOrder는 보안상 임의 코드를 실행하지 않고, 모든 노드가 동일하게 재현할 수 있는 내장 컨트랙트 유형을 온체인 트랜잭션으로 처리합니다.</p>
      <h2>지원 컨트랙트</h2>
      <h3>counter</h3>
      <ul>
        <li><code>increment</code>: 값을 증가시킵니다. 예: <code>{"amount": 1}</code></li>
        <li><code>decrement</code>: 값을 감소시킵니다.</li>
        <li><code>reset</code>: 값을 0으로 초기화합니다.</li>
      </ul>
      <h3>key_value</h3>
      <ul>
        <li><code>set</code>: <code>{"key": "name", "value": "alice"}</code> 값을 저장합니다.</li>
        <li><code>delete</code>: <code>{"key": "name"}</code> 값을 삭제합니다.</li>
      </ul>
      <h2>컨트랙트 배포</h2>
      ${code('./neworder_wallet contract-deploy --wallet wallet1.json --name VoteCounter --type counter\n./neworder_wallet mine --wallet wallet1.json')}
      <h2>컨트랙트 호출</h2>
      ${code('./neworder_wallet contract-call --wallet wallet1.json --contract NOC... --method increment --amount 3\n./neworder_wallet mine --wallet wallet1.json')}
      <h2>상태 조회</h2>
      ${code('curl http://127.0.0.1:8080/contracts/NOC...')}
    `,
  },
  tokens: {
    title: "토큰 발행",
    html: `
      <p>NewOrder는 네이티브 코인 NO 위에서 사용자 정의 토큰을 발행할 수 있습니다.</p>
      <h2>토큰 발행</h2>
      ${code('./neworder_wallet token-create --wallet wallet1.json --name "Example Token" --symbol EXT --supply 1000000\n./neworder_wallet mine --wallet wallet1.json')}
      <p>PoA 블록 생성 후 <code>GET /tokens</code>에서 <code>NOT...</code> 형식의 <code>token_id</code>를 확인합니다. 발행자는 초기 공급량 전체를 보유합니다.</p>
      <h2>토큰 전송</h2>
      ${code('./neworder_wallet token-send --wallet wallet1.json --token NOT... --to NO... --amount 100\n./neworder_wallet mine --wallet wallet1.json')}
      <h2>토큰 잔액 조회</h2>
      ${code('curl http://127.0.0.1:8080/tokens/NOT.../balances/NO...')}
      <h2>검증 규칙</h2>
      <ul>
        <li>토큰 심볼은 1~12자의 영문/숫자여야 합니다.</li>
        <li>토큰 심볼은 네이티브 코인 심볼 <code>NO</code>와 같을 수 없습니다.</li>
        <li>총 공급량은 0보다 커야 합니다.</li>
        <li>소수점 자릿수는 0~18 사이여야 합니다.</li>
        <li>토큰 전송자는 충분한 토큰 잔액을 보유해야 합니다.</li>
      </ul>
    `,
  },
  api: {
    title: "Explorer API",
    html: `
      <p>기본 주소는 노드 실행 시 지정한 HTTP 주소입니다. 기본값은 <code>http://127.0.0.1:8080</code>입니다.</p>
      <h2>체인 조회</h2>
      <ul>
        <li><code>GET /stats</code>: 코인, 높이, 최신 해시, PoA validator, 메모리풀, 토큰/컨트랙트 수</li>
        <li><code>GET /chain</code>: 전체 블록 목록</li>
        <li><code>GET /blocks/{height-or-hash}</code>: 특정 블록</li>
        <li><code>GET /tx/{txid}</code>: 트랜잭션 검색</li>
        <li><code>GET /mempool</code>: 대기 트랜잭션</li>
      </ul>
      <h2>주소와 토큰</h2>
      <ul>
        <li><code>GET /address/{address}</code>: NO 잔액과 토큰 잔액</li>
        <li><code>GET /tokens</code>: 발행된 토큰 목록</li>
        <li><code>GET /tokens/{token_id}</code>: 특정 토큰 정보</li>
        <li><code>GET /tokens/{token_id}/balances/{address}</code>: 특정 주소의 토큰 잔액</li>
      </ul>
      <h2>컨트랙트와 노드</h2>
      <ul>
        <li><code>GET /contracts</code>: 배포된 컨트랙트 목록</li>
        <li><code>GET /contracts/{contract_id}</code>: 컨트랙트 메타데이터와 현재 상태</li>
        <li><code>GET /mine?address={NO...}</code>: 현재 담당 validator 노드가 PoA 블록을 생성하고 보상을 지정 주소로 지급</li>
        <li><code>POST /transactions</code>: 트랜잭션 제출</li>
      </ul>
      <h2>AI 결제</h2>
      <ul>
        <li><code>GET /ai/services</code>: 이용 가능한 AI 서비스 목록</li>
        <li><code>GET /ai/payments/{payment_id}</code>: 결제 요청 조회</li>
        <li><code>POST /ai/payments</code>: 결제 요청 생성</li>
        <li><code>POST /ai/payments/{payment_id}/verify</code>: 결제 검증</li>
        <li><code>POST /ai/payments/{payment_id}/consume</code>: 서비스 사용</li>
      </ul>
    `,
  },
};

function code(text) {
  const escaped = text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
  return `<div class="code-block"><button class="copy" type="button">복사</button><pre><code>${escaped}</code></pre></div>`;
}

const article = document.getElementById("article");
const title = document.getElementById("page-title");
const nav = document.getElementById("nav");
const search = document.getElementById("search");

function render(pageKey, query = "") {
  const page = pages[pageKey] || pages.overview;
  title.textContent = page.title;
  article.innerHTML = query ? highlight(page.html, query) : page.html;
  document.querySelectorAll(".nav button").forEach((button) => {
    button.classList.toggle("active", button.dataset.page === pageKey);
  });
  attachCopyButtons();
}

function highlight(html, query) {
  const safe = query.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  if (!safe) return html;
  return html.replace(new RegExp(`(${safe})`, "gi"), "<mark>$1</mark>");
}

function attachCopyButtons() {
  document.querySelectorAll(".copy").forEach((button) => {
    button.addEventListener("click", () => {
      const text = button.nextElementSibling.innerText;
      copyText(text);
      button.textContent = "완료";
      setTimeout(() => {
        button.textContent = "복사";
      }, 1200);
    });
  });
}

function copyText(text) {
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text);
    return;
  }
  const textarea = document.createElement("textarea");
  textarea.value = text;
  textarea.setAttribute("readonly", "");
  textarea.style.position = "fixed";
  textarea.style.opacity = "0";
  document.body.appendChild(textarea);
  textarea.select();
  document.execCommand("copy");
  textarea.remove();
}

nav.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-page]");
  if (!button) return;
  location.hash = button.dataset.page;
  render(button.dataset.page, search.value.trim());
});

search.addEventListener("input", () => {
  const current = location.hash.slice(1) || "overview";
  render(current, search.value.trim());
});

window.addEventListener("hashchange", () => {
  render(location.hash.slice(1) || "overview", search.value.trim());
});

render(location.hash.slice(1) || "overview");
