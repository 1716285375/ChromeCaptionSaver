const status=document.querySelector('#status'),input=document.querySelector('#code');
status.textContent='扩展版本 0.3.7 · 请绑定当前 ChatGPT 页面';
chrome.storage.local.get(['port','token']).then(c=>{if(c.port&&c.token)input.value=`${c.port}:${c.token}`;});
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
document.querySelector('#bind').addEventListener('click',async()=>{
  try {
    const match=input.value.trim().match(/^(\d{1,5}):([a-f0-9]{64})$/);
    if(!match || +match[1]<1 || +match[1]>65535)throw new Error('连接码格式不正确');
    const [tab]=await chrome.tabs.query({active:true,currentWindow:true});
    if(!tab?.id || new URL(tab.url).origin!=='https://chatgpt.com')throw new Error('请先切换到 ChatGPT 标签页，再打开此扩展');
    await chrome.storage.local.set({port:+match[1],token:match[2],tabId:tab.id});
    const health=await chrome.runtime.sendMessage({type:'bridge',route:'/health',payload:{}});
    if(!health?.ok || health.result?.inactive)throw new Error(health?.error??'桌面程序未连接');
    status.textContent='桌面已连接，正在检查网页脚本…';
    const ping=()=>chrome.tabs.sendMessage(tab.id,{type:'ccs-ping'},{frameId:0});
    let page;
    try{page=await ping();}catch{}
    if(page?.version!=='0.3.7') {
      const results=await chrome.scripting.executeScript({target:{tabId:tab.id,frameIds:[0]},func:()=>
        [...document.querySelectorAll('[contenteditable="true"],textarea')].some(n=>n.getClientRects().length>0 && (n.value??n.innerText??'').trim())});
      if(results.some(r=>r.result))throw new Error('页面仍有未发送草稿，请先保存或清空，再点击绑定；不会自动刷新丢失草稿');
      status.textContent='正在刷新页面，清除失效的旧脚本…';
      await chrome.tabs.reload(tab.id);
    }
    for(let i=0;i<40;++i){try{page=await ping();}catch{page=null;}if(page?.version==='0.3.7' && page?.lastPoll && Date.now()-page.lastPoll<6000)break;await wait(300);}
    if(page?.version!=='0.3.7' || !page?.lastPoll || Date.now()-page.lastPoll>=6000)throw new Error(page?.error||'网页未连接桌面；请确认扩展已重新加载后再绑定');
    if(!page.ready)throw new Error('网页连接正常，但找不到可用输入框。请登录 ChatGPT 并打开聊天页面后重新绑定');
    status.textContent='扩展 / 页面脚本 0.3.7 · '+(page.draft?'已连接网页。请清空已有草稿后在 EXE 启动联动。':page.busy?'已连接网页。当前回答结束后会继续处理字幕。':'网页与 EXE 已双向连接。回到 EXE 点击“启动联动”。');
  }catch(error){status.textContent='连接未完成：'+error.message;}
});
document.querySelector('#disconnect').addEventListener('click',async()=>{await chrome.storage.local.remove(['port','token','tabId']);input.value='';status.textContent='已解除绑定。';});
