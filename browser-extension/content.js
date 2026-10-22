(() => {
  if(globalThis.__ccsRelay?.version==='0.3.7')return;
  globalThis.__ccsRelay?.dispose();
  let stopped=false,timer=null,lastWarning='',lastWarningAt=0;
  const dispose=()=>{stopped=true;clearTimeout(timer);try{chrome.runtime.onMessage.removeListener(onMessage);}catch{}};
  globalThis.__ccsRelay={version:'0.3.7',dispose};
  const client=crypto.randomUUID();
  let lastPoll=0,lastError='尚未收到桌面任务状态';
  const onMessage=(message,sender,respond)=>{
    if(message?.type!=='ccs-ping')return;
    const dom=CaptionChatBridge.driver(document);
    respond({version:'0.3.7',lastPoll,error:lastError,ready:dom.ready(),busy:dom.busy(),draft:!!dom.draft()});
  };
  chrome.runtime.onMessage.addListener(onMessage);
  const api=async(route,body)=>{
    if(stopped)throw new Error('页面联动脚本已停止');
    const result=await chrome.runtime.sendMessage({type:'bridge',route,payload:{client,url:location.href,...body}});
    if(!result?.ok)throw Object.assign(new Error(result?.error??'扩展连接已失效'),{retryable:result?.retryable===true});
    if(!result.result?.inactive)lastPoll=Date.now();
    return result.result;
  };
  let boundEpoch=null,pinnedPath=null;
  async function loop() {
    if(stopped)return;
    let task;
    try {
      if(!chrome.runtime.id){dispose();return;}
      const dom=CaptionChatBridge.driver(document);
      const result=await api('/poll',{busy:dom.busy(),ready:dom.ready(),draft:!!dom.draft()});
      if(!result.inactive){lastPoll=Date.now();lastError='';}
      if(result.inactive || !result.enabled){boundEpoch=null;pinnedPath=null;return;}
      if(boundEpoch!==result.epoch){boundEpoch=result.epoch;pinnedPath=/\/c\/[^/]+/.test(location.pathname)?location.pathname:null;}
      if(result.action==='send')task=result;
      if(pinnedPath && pinnedPath!==location.pathname)throw new Error('已切换 ChatGPT 会话，请在桌面端重启联动');
      if(result.action!=='send')return;
      task=result;
      await CaptionChatBridge.runTask(task,api,dom);
      pinnedPath=location.pathname;
    } catch(error) {
      lastError=error.message;
      if(/Extension context invalidated/i.test(error.message)) {dispose();return;}
      if(lastWarning!==lastError || Date.now()-lastWarningAt>=60000){
        console.warn('[ChromeCaptionSaver 0.3.7]',error.message);lastWarning=lastError;lastWarningAt=Date.now();
      }
      if(task)try{await api('/error',{id:task.id,error:error.message});}catch{}
      // No automatic resend: the desktop keeps the uncertain task paused.
    } finally {if(!stopped)timer=setTimeout(loop,1000);}
  }
  loop();
})();
