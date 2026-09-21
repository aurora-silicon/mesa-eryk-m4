const v=document.querySelector('video'),p=JSON.parse(document.documentElement.dataset.m4Perf);
const d=(a)=>a.slice(1).map((t,i)=>t-a[i]).sort((a,b)=>a-b);
const stat=(a)=>({count:a.length,median:a[Math.floor(a.length*.5)],p95:a[Math.floor(a.length*.95)],max:Math.max(...a)});
const samples=(p.samples||[]).filter(s=>s.t>=p.start+8000),first=samples[0],last=samples.at(-1);
const steady=first&&last&&last.time>first.time?{seconds:last.time-first.time,total:last.total-first.total,dropped:last.dropped-first.dropped,notDroppedPerSecond:(last.total-first.total-last.dropped+first.dropped)/(last.time-first.time),resolutions:[...new Set(samples.map(s=>s.width+'x'+s.height))]}:null;
return {steady,elapsed:(performance.now()-p.start)/1000,mediaTime:v.currentTime,paused:v.paused,width:v.videoWidth,height:v.videoHeight,quality:v.getVideoPlaybackQuality(),initialQuality:p.quality,raf:stat(d(p.raf)),video:stat(d(p.video.map(v=>v.t))),decodeMs:stat(p.video.filter(v=>Number.isFinite(v.processingDuration)).map(v=>v.processingDuration*1000).sort((a,b)=>a-b)),videoFrames:p.video,error:p.error,visibility:document.visibilityState,focus:document.hasFocus()};
