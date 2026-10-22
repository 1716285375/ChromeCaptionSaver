(function(root) {
  const delay = ms => new Promise(resolve=>setTimeout(resolve,ms));
  const clean = value => String(value??'').replace(/\s+/gu,' ').trim();
  const conversationPath = path => /\/c\/[^/]+/.test(path);
  function driver(document) {
    const visible = node => !!node && node.getClientRects().length>0 && !node.closest('[hidden],[aria-hidden="true"]');
    const editor = () => [...document.querySelectorAll('#prompt-textarea[contenteditable="true"],textarea#prompt-textarea,#prompt-textarea [contenteditable="true"],textarea[data-id="root"],[contenteditable="true"][role="textbox"],.ProseMirror[contenteditable="true"]')].find(visible);
    const text = node => node ? ('value' in node ? node.value : node.innerText ?? node.textContent ?? '') : '';
    const composer = () => editor()?.closest('form') ?? editor()?.closest('[data-composer-body]');
    const buttons = () => [...(composer() ?? document).querySelectorAll('button')];
    const busy = () => [...document.querySelectorAll('[data-testid="stop-button"],button[aria-label="Stop streaming"],button[aria-label="Stop generating"],button[aria-label="停止生成"],[data-is-streaming="true"]')].some(visible);
    const usableSend = b => visible(b) && !/voice|语音|听写|dictat|stop|停止/i.test(b.getAttribute('aria-label')??'');
    const sendButton = () => buttons().find(b=>usableSend(b)&&(
      /^(Send|Send prompt|Send message|发送|发送提示|发送提示词|发送消息|提交|傳送)$/i.test((b.getAttribute('aria-label')??b.getAttribute('title')??'').trim()) ||
      b.getAttribute('data-testid')==='send-button' || b.id==='composer-submit-button')) ??
      (composer() ? [...composer().querySelectorAll('button[type="submit"]')].find(usableSend) : undefined);
    const assistants = () => {
      return [...document.querySelectorAll('[data-message-author-role="assistant"],[data-conversation-role="assistant"]')].map(n=>n.closest('[data-message-author-role="assistant"]') ?? n.parentElement)
        .filter((n,i,all)=>n&&all.indexOf(n)===i);
    };
    const users = () => [...document.querySelectorAll('[data-message-author-role="user"],[data-user-message-bubble="true"]')];
    const replyText = node => {
      const markdown=[...node.querySelectorAll('[class*="MarkdownRoot-"]')];
      return markdown.length?markdown.map(text).join('\n'):text(node);
    };
    let userCountBefore=0,replyCountBefore=0,lastUserBefore='',lastReplyBefore='';
    let replyNodesBefore=new Set();
    return {
      ready:()=>!!editor(), draft:()=>clean(text(editor())), busy,
      replies:()=>assistants().map(replyText),
      user:()=>text(users().at(-1)), path:()=>document.location.pathname,
      fill(prompt) {
        const node = editor(); if (!node) throw new Error('找不到 ChatGPT 输入框，请先登录');
        if (clean(text(node))) throw new Error('输入框已有草稿，未覆盖');
        node.focus();
        if ('value' in node) {
          const setter = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value').set;
          setter.call(node,prompt); node.dispatchEvent(new Event('input',{bubbles:true}));
        } else {
          const selection = document.getSelection(), range = document.createRange();
          range.selectNodeContents(node); selection.removeAllRanges(); selection.addRange(range);
          if (!document.execCommand('insertText',false,prompt)) throw new Error('网页未接受输入，草稿未提交');
          node.dispatchEvent(new InputEvent('input',{bubbles:true,inputType:'insertText',data:prompt}));
        }
      },
      matches:prompt=>clean(text(editor()))===clean(prompt),
      canSend:()=>{const b=sendButton();return b && !b.disabled && b.getAttribute('aria-disabled')!=='true';},
      send:()=>{const b=sendButton();if(!b || b.disabled || b.getAttribute('aria-disabled')==='true')throw new Error('发送按钮不可用');userCountBefore=users().length;replyCountBefore=assistants().length;lastUserBefore=text(users().at(-1));replyNodesBefore=new Set(assistants());lastReplyBefore=assistants().at(-1)?replyText(assistants().at(-1)):'';b.click();},
      response:()=>{
        const last=assistants().at(-1),user=users().at(-1);
        if(!last || (user && !(user.compareDocumentPosition(last)&4)))return '';
        const reply=replyText(last);
        return !replyNodesBefore.has(last) || reply!==lastReplyBefore ? reply : '';
      },
      submitted:()=>!clean(text(editor())) && (busy() || users().length>userCountBefore || text(users().at(-1))!==lastUserBefore || assistants().length>replyCountBefore),
      complete:()=>{const last=assistants().at(-1);const turn=last?.closest('[data-talvt-turn-state]');return !turn || turn.getAttribute('data-talvt-turn-state')==='complete';},
      diagnostics:()=>{const b=sendButton();return !b?'未找到发送按钮':b.disabled || b.getAttribute('aria-disabled')==='true'?'发送按钮被网页禁用':'发送按钮已就绪';},
      error:()=>document.querySelector('[data-testid="conversation-turn-error"], [data-testid="error-message"]')?.textContent ?? ''
    };
  }
  async function runTask(task, api, dom, settings={}) {
    const pollMs=settings.pollMs??500, settleMs=settings.settleMs??1800, maxWaitMs=settings.maxWaitMs??600000;
    let stage='正在提交';
    const request = async(route,body)=>{
      const deadline=Date.now()+(settings.transportWaitMs??120000);
      for(;;) {
        try {return await api(route,body);}
        catch(error) {
          if(!error.retryable || Date.now()>=deadline)throw error;
          await delay(Math.min(pollMs*2,2000));
        }
      }
    };
    const ensureEnabled = async()=>{
      let h;
      for(let attempt=0;attempt<3;++attempt) {
        try {
          const replies=dom.replies(),reply=dom.response?dom.response():replies.at(-1)??'';
          h=await request('/health',{id:task.id,progress:`${stage}；回答节点 ${replies.length}，新回答 ${reply.length} 字，生成中 ${dom.busy()?'是':'否'}，结束标记 ${!dom.complete || dom.complete()?'是':'否'}`});break;
        }
        catch(error) {
          if(attempt===2 || /Extension context invalidated/i.test(error.message))throw error;
          await delay(pollMs);
        }
      }
      if(!h.enabled || h.epoch!==task.epoch)throw new Error('联动已停止，未提交的草稿保留');
    };
    await ensureEnabled();
    if(!dom.ready() || dom.busy() || dom.draft())throw new Error('网页未就绪、正在回答或存在草稿，未提交');
    const before=dom.replies(), count=before.length, originalPath=dom.path();
    dom.fill(task.prompt);
    const begin=Date.now();
    while(!dom.matches(task.prompt) || !dom.canSend()) {
      if(Date.now()-begin>(settings.inputWaitMs??10000))throw new Error(!dom.matches(task.prompt)?'填入的文本与提示词不一致，未提交':(dom.diagnostics?.()??'发送按钮不可用')+'，未提交');
      await ensureEnabled();await delay(pollMs);
    }
    await ensureEnabled();
    if(dom.path()!==originalPath || !dom.matches(task.prompt))throw new Error('页面或输入内容发生变化，未提交');
    dom.send(); // Exactly once: a failed/uncertain acknowledgement never causes another click.
    const clickedAt=Date.now();
    while(dom.submitted && !dom.submitted(task.prompt)) {
      if(Date.now()-clickedAt>(settings.submitWaitMs??10000))throw new Error('已点击发送，但网页未确认提交；不会自动重复点击');
      await ensureEnabled();await delay(pollMs);
    }
    await request('/sent',{id:task.id});
    stage='等待网页回答';
    let previous='',stableAt=Date.now(), pinnedPath=conversationPath(originalPath)?originalPath:null;
    while(Date.now()-begin<maxWaitMs) {
      await delay(pollMs); await ensureEnabled();
      const path=dom.path();
      if(pinnedPath && path!==pinnedPath)throw new Error('ChatGPT 已切换会话，停止自动发送');
      if(!pinnedPath && conversationPath(path) && clean(dom.user()).startsWith(clean(task.prompt)))pinnedPath=path;
      const error=dom.error();if(error)throw new Error(error.slice(0,300));
      const replies=dom.replies(),reply=dom.response?dom.response():replies.at(-1)??'';
      const isNew=dom.response?!!reply:replies.length>count || reply!==(before.at(-1)??'');
      if(reply!==previous || dom.busy()){previous=reply;stableAt=Date.now();}
      if(isNew && clean(reply) && !dom.busy() && (!dom.complete || dom.complete()) && Date.now()-stableAt>=settleMs) {
        stage='已读取回答，正在回传';
        await request('/complete',{id:task.id,reply,epoch:task.epoch});return reply;
      }
    }
    throw new Error('等待回答超时，不自动重复提交');
  }
  root.CaptionChatBridge={driver,runTask,clean};
  if(typeof module!=='undefined')module.exports=root.CaptionChatBridge;
})(globalThis);
