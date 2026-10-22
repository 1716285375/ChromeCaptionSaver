const routes = new Set(['/health', '/poll', '/sent', '/complete', '/error']);
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (message?.type !== 'bridge') return;
  (async () => {
    if (sender.id !== chrome.runtime.id || message.type !== 'bridge' || !routes.has(message.route)) throw new Error('不支持的请求');
    const config = await chrome.storage.local.get(['port','token','tabId']);
    if (!config.token || !config.port) return {inactive:true};
    if (sender.tab) {
      if (sender.tab.id !== config.tabId || sender.frameId !== 0 || new URL(sender.url).origin !== 'https://chatgpt.com') return {inactive:true};
    } else if (message.route !== '/health') throw new Error('只允许绑定的 ChatGPT 页面请求任务');
    if (!Number.isInteger(config.port) || config.port < 1 || config.port > 65535 || !/^[a-f0-9]{64}$/.test(config.token)) throw new Error('连接码无效');
    const response = await fetch(`http://127.0.0.1:${config.port}${message.route}`, {
      method:'POST', credentials:'omit', cache:'no-store', signal:AbortSignal.timeout(5000),
      headers:{'Content-Type':'application/json','Authorization':`Bearer ${config.token}`}, body:JSON.stringify(message.payload ?? {})
    });
    if (!response.ok) throw Object.assign(new Error(`桌面应用 ${message.route} 返回 ${response.status}，任务或绑定状态不匹配`),{retryable:response.status>=500});
    return response.json();
  })().then(result=>respond({ok:true,result})).catch(error=>respond({ok:false,error:error.message,retryable:error.retryable??/fetch|timeout|timed out|aborted/i.test(error.message)}));
  return true;
});
