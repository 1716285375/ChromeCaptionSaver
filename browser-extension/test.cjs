const {test}=require('node:test');
const assert=require('node:assert/strict');
const {runTask,driver}=require('./bridge-core.js');
const vm=require('node:vm'),fs=require('node:fs');
async function bindFixture(connected,hasDraft=false){
  const handlers={},status={textContent:''},code={value:'12345:'+ 'a'.repeat(64)};
  let injected=0;
  const chrome={storage:{local:{get:async()=>({}),set:async()=>{},remove:async()=>{}}},
    runtime:{sendMessage:async()=>({ok:true,result:{enabled:false}})},
    tabs:{query:async()=>[{id:4,url:'https://chatgpt.com/'}],reload:async()=>{injected++;},sendMessage:async()=>{
      if(!injected)throw Error('Receiving end does not exist');
      return {version:'0.3.7',lastPoll:connected?Date.now():0,error:'网页连接失败',ready:true};
    }},scripting:{executeScript:async()=>[{result:hasDraft}]}};
  const document={querySelector:id=>id==='#status'?status:id==='#code'?code:{addEventListener:(event,fn)=>handlers[id]=fn}};
  vm.runInNewContext(fs.readFileSync(require.resolve('./popup.js'),'utf8'),{chrome,document,URL,Date,setTimeout:fn=>fn()});
  await handlers['#bind']();return {status:status.textContent,injected};
}
test('binding refreshes a stale page and verifies its desktop poll',async()=>{
  const result=await bindFixture(true);assert.equal(result.injected,1);assert.match(result.status,/双向连接/);
});
test('binding preserves a draft instead of reloading it away',async()=>{
  const result=await bindFixture(true,true);assert.equal(result.injected,0);assert.match(result.status,/草稿/);
});
function submittedDriver(label='发送'){
  const users=[];
  const form={querySelectorAll:()=>[button]};
  const editor={value:'Translate hello',getClientRects:()=>[{}],closest:s=>s==='form'?form:null};
  const button={id:'composer-submit-button',disabled:false,getClientRects:()=>[{}],closest:()=>null,
    getAttribute:k=>k==='aria-label'?label:null,click:()=>{editor.value='';users.push({innerText:'Translate hello 附加标签'});}};
  const doc={location:{pathname:'/c/test'},querySelectorAll:s=>s.includes('contenteditable')?[editor]:s.includes('data-message-author-role="user"')?users:[]};
  return driver(doc);
}
test('new user message plus cleared draft confirms submission despite added labels',()=>{
  const dom=submittedDriver();dom.send();assert.equal(dom.submitted('Translate hello'),true);
});
test('legacy composer id cannot make the voice button a send button',()=>{
  const dom=submittedDriver('开始语音');assert.ok(!dom.canSend());
});
test('desktop health alone never claims a successful page binding',async()=>{
  const result=await bindFixture(false);assert.match(result.status,/连接未完成/);assert.ok(!result.status.includes('双向连接'));
});
test('hidden stop markers do not block sending; only editable visible composers qualify',()=>{
  const editor={getClientRects:()=>[{}],closest:()=>null,value:''};
  const hiddenStop={getClientRects:()=>[],closest:()=>null};
  const button={id:'composer-submit-button',getClientRects:()=>[{}],closest:()=>null,disabled:false,getAttribute:()=>null};
  const doc={querySelectorAll:s=>s.includes('contenteditable')?[editor]:s.includes('stop-button')?[hiddenStop]:s==='button'?[button]:[],querySelector:()=>hiddenStop};
  const dom=driver(doc);assert.equal(dom.ready(),true);assert.equal(dom.busy(),false);assert.equal(dom.canSend(),true);
});
function fixture(){
  let clicks=0,filled='',enabled=true,replies=[],path='/c/test',draft='',busy=false;
  const calls=[];
  const api=async(route,body)=>{calls.push(route);return route==='/health'?{enabled,epoch:1}:{accepted:true};};
  const dom={ready:()=>true,draft:()=>draft,busy:()=>busy,replies:()=>replies,user:()=>filled,path:()=>path,
    fill:s=>{filled=s;},matches:s=>s===filled,canSend:()=>true,send:()=>{clicks++;replies=['翻译'];},error:()=>''};
  return {dom,api,calls,get clicks(){return clicks;},stop:()=>enabled=false,draft:()=>draft='mine',move:()=>path='/c/other',busy:()=>busy=true};
}
const task={id:1,epoch:1,prompt:'Translate\n\nHello.'};
// Windows CI timer scheduling can exceed 40 ms across two polling ticks.
const options={pollMs:1,settleMs:2,maxWaitMs:1000};

test('a shared turn containing the user and answer does not discard the answer',()=>{
  const markdown={innerText:'Answer body'};
  const turn={querySelectorAll:()=>[markdown],closest:()=>turn,getAttribute:()=> 'complete'};
  const body={querySelectorAll:()=>[markdown],closest:s=>s==='[data-talvt-turn-state]'?turn:null};
  const role={parentElement:body,closest:s=>s==='[data-talvt-turn-state]'?turn:null};
  const user={compareDocumentPosition:n=>n===turn?10:n===body?4:2};
  const doc={querySelectorAll:s=>s==='[data-message-author-role="assistant"],[data-conversation-role="assistant"]'?[role]:s.includes('data-user-message-bubble')?[user]:[]};
  const dom=driver(doc);
  assert.equal(dom.response(),'Answer body');assert.equal(dom.complete(),true);
  user.compareDocumentPosition=()=>2;
  assert.equal(dom.response(),'');
});

test('virtualized answers complete when the number of mounted replies stays constant',async()=>{
  const f=fixture();let reply='Old answer';
  f.dom.replies=()=>[reply];f.dom.send=()=>{reply='New answer';};
  assert.equal(await runTask(task,f.api,f.dom,options),'New answer');
  assert.ok(f.calls.includes('/complete'));
});
test('completed reply is retried after a lost HTTP response without another send click',async()=>{
  const f=fixture();let attempts=0;
  const api=async(r,b)=>{if(r==='/complete' && ++attempts===1)throw Object.assign(Error('Failed to fetch'),{retryable:true});return f.api(r,b);};
  assert.equal(await runTask(task,api,f.dom,options),'翻译');
  assert.equal(attempts,2);assert.equal(f.clicks,1);
});
test('one click, acknowledgement before completed reply',async()=>{
  const f=fixture();assert.equal(await runTask(task,f.api,f.dom,options),'翻译');
  assert.equal(f.clicks,1);assert.ok(f.calls.indexOf('/sent')<f.calls.indexOf('/complete'));
});
test('a transient health timeout preserves the submitted answer without another click',async()=>{
  const f=fixture();let timedOut=false;
  const api=async(r,b)=>{
    if(r==='/health' && f.calls.includes('/sent') && !timedOut){timedOut=true;throw Error('signal timed out');}
    return f.api(r,b);
  };
  assert.equal(await runTask(task,api,f.dom,options),'翻译');
  assert.equal(timedOut,true);assert.equal(f.clicks,1);assert.ok(f.calls.includes('/complete'));
});
test('persistent health failure is bounded and never resends',async()=>{
  const f=fixture();let failures=0;
  const api=async(r,b)=>{
    if(r==='/health' && f.calls.includes('/sent')){failures++;throw Error('signal timed out');}
    return f.api(r,b);
  };
  await assert.rejects(runTask(task,api,f.dom,options),/signal timed out/);
  assert.equal(failures,3);assert.equal(f.clicks,1);assert.ok(!f.calls.includes('/complete'));
});
test('draft remains untouched',async()=>{const f=fixture();f.draft();await assert.rejects(runTask(task,f.api,f.dom,options));assert.equal(f.clicks,0);});
test('stopped before send',async()=>{const f=fixture();f.stop();await assert.rejects(runTask(task,f.api,f.dom,options));assert.equal(f.clicks,0);});
test('stop during generation never completes or retries',async()=>{
  const f=fixture();f.dom.send=()=>{f.stop();};await assert.rejects(runTask(task,f.api,f.dom,options));assert.ok(!f.calls.includes('/complete'));
});
test('lost acknowledgement never clicks twice',async()=>{
  const f=fixture();const api=async(r,b)=>{if(r==='/sent')throw Error('lost');return f.api(r,b);};
  await assert.rejects(runTask(task,api,f.dom,options));assert.equal(f.clicks,1);
});
test('changed conversation stops completion',async()=>{
  const f=fixture();const send=f.dom.send;f.dom.send=()=>{send();f.move();};
  await assert.rejects(runTask(task,f.api,f.dom,options));assert.equal(f.clicks,1);assert.ok(!f.calls.includes('/complete'));
});
test('streaming timeout never completes',async()=>{
  const f=fixture();const send=f.dom.send;f.dom.send=()=>{send();f.busy();};
  await assert.rejects(runTask(task,f.api,f.dom,options));assert.equal(f.clicks,1);assert.ok(!f.calls.includes('/complete'));
});
test('an ignored send click never acknowledges submission or retries',async()=>{
  const f=fixture();f.dom.submitted=()=>false;
  await assert.rejects(runTask(task,f.api,f.dom,{...options,submitWaitMs:5}),/未确认提交/);
  assert.equal(f.clicks,1);assert.ok(!f.calls.includes('/sent'));assert.ok(!f.calls.includes('/complete'));
});
test('incomplete modern turn cannot be reported as a finished answer',async()=>{
  const f=fixture();f.dom.complete=()=>false;
  await assert.rejects(runTask(task,f.api,f.dom,options));assert.ok(!f.calls.includes('/complete'));
});
async function contentFixture(invalidated,duplicate=false){
  const stats={requests:0,timers:0,listeners:0,removed:0,warnings:0};
  const chrome={runtime:{id:'extension',onMessage:{addListener:()=>stats.listeners++,removeListener:()=>stats.removed++},
    sendMessage:async()=>{stats.requests++;if(invalidated)throw Error('Extension context invalidated.');return {ok:true,result:{inactive:true}};}}};
  const context=vm.createContext({chrome,crypto:{randomUUID:()=> 'test-client'},location:{href:'https://chatgpt.com/c/test',pathname:'/c/test'},document:{},
    CaptionChatBridge:{driver:()=>({busy:()=>false,ready:()=>true,draft:()=>''})},
    setTimeout:()=>{stats.timers++;return 1;},clearTimeout:()=>{},console:{warn:()=>stats.warnings++}});
  const source=fs.readFileSync(require.resolve('./content.js'),'utf8');
  vm.runInContext(source,context);if(duplicate)vm.runInContext(source,context);
  await new Promise(resolve=>setImmediate(resolve));return stats;
}
test('invalidated extension context exits without another timer or repeated warnings',async()=>{
  const s=await contentFixture(true);assert.equal(s.requests,1);assert.equal(s.timers,0);assert.equal(s.removed,1);assert.equal(s.warnings,0);
});
test('repeated script injection installs only one polling loop',async()=>{
  const s=await contentFixture(false,true);assert.equal(s.listeners,1);assert.equal(s.requests,1);assert.equal(s.timers,1);
});
