const result = {url: location.href, title: document.title, body: document.body.innerText.slice(0, 6000),
  globals: Object.keys(window).filter(x => /shader|effect|renderer/i.test(x)),
  canvases: Array.from(document.querySelectorAll('canvas')).map(c => ({id:c.id,width:c.width,height:c.height}))};
result.contexts = ['webgl2', 'webgl'].map(kind => {
  const c = document.createElement('canvas'); c.width=16; c.height=16;
  let failure = null;
  c.addEventListener('webglcontextcreationerror', e => {failure=e.statusMessage;});
  const gl = c.getContext(kind, {failIfMajorPerformanceCaveat:true});
  if (!gl) return {kind,context:false,failure};
  const debug=gl.getExtension('WEBGL_debug_renderer_info');
  const row={kind,context:true,lost:gl.isContextLost(),renderer:debug?gl.getParameter(debug.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER),attributes:gl.getContextAttributes(),extensions:gl.getSupportedExtensions()};
  gl.clearColor(0.25,0.5,0.75,1);gl.clear(gl.COLOR_BUFFER_BIT);
  const pixels=new Uint8Array(4);gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
  row.pixel=Array.from(pixels);row.error=gl.getError();
  gl.getExtension('WEBGL_lose_context')?.loseContext();
  return row;
});
return result;
