const v=document.querySelector('video'),p=JSON.parse(document.documentElement.dataset.m4Perf);
const d=(a)=>a.slice(1).map((t,i)=>t-a[i]).sort((a,b)=>a-b);
const stat=(a)=>({count:a.length,median:a[Math.floor(a.length*.5)],p95:a[Math.floor(a.length*.95)],max:Math.max(...a)});
return {elapsed:(performance.now()-p.start)/1000,mediaTime:v.currentTime,paused:v.paused,width:v.videoWidth,height:v.videoHeight,quality:v.getVideoPlaybackQuality(),initialQuality:p.quality,raf:stat(d(p.raf)),video:stat(d(p.video.map(v=>v.t))),decodeMs:stat(p.video.filter(v=>Number.isFinite(v.processingDuration)).map(v=>v.processingDuration*1000).sort((a,b)=>a-b)),videoFrames:p.video,error:p.error,visibility:document.visibilityState,focus:document.hasFocus()};
