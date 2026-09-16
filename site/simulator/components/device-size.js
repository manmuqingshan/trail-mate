export function fitDeviceSize(viewBox, availableWidth) {
  const width=Math.min(480,Math.max(1,availableWidth-24),560*viewBox.width/viewBox.height);
  return {width,height:width*viewBox.height/viewBox.width};
}
