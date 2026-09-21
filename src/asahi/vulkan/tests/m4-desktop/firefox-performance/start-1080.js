const element=document.getElementById('movie_player'),player=element?.wrappedJSObject||element;
if(!player || typeof player.setPlaybackQualityRange!=='function')throw Error('YouTube quality control unavailable');
player.setPlaybackQualityRange('hd1080','hd1080');
const v=document.querySelector('video');if(!v)throw Error('No video');
const p={start:performance.now(),quality:{totalVideoFrames:v.getVideoPlaybackQuality().totalVideoFrames,droppedVideoFrames:v.getVideoPlaybackQuality().droppedVideoFrames},time:v.currentTime,raf:[],video:[]};
function raf(t){p.raf.push(t);if(p.raf.length<4000)requestAnimationFrame(raf);}requestAnimationFrame(raf);
function frame(t,m){p.video.push({t,mediaTime:m.mediaTime,presentedFrames:m.presentedFrames,processingDuration:m.processingDuration});v.requestVideoFrameCallback(frame);}v.requestVideoFrameCallback(frame);
function play(){v.muted=true;if(v.paused)v.play().catch(e=>p.error=String(e));}play();window.playProbeTimer=setInterval(play,1000);


p.samples=[];
setInterval(()=>{
 const q=v.getVideoPlaybackQuality();
 p.samples.push({t:performance.now(),time:v.currentTime,total:q.totalVideoFrames,dropped:q.droppedVideoFrames,width:v.videoWidth,height:v.videoHeight});
 document.documentElement.dataset.m4Perf=JSON.stringify(p);
},250);
return {start:p.start,quality:p.quality,time:v.currentTime};
