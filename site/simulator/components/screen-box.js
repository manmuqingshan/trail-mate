export function containScreen(opening, pixelWidth, pixelHeight) {
  const scale=Math.min(opening.width/pixelWidth,opening.height/pixelHeight);
  const width=pixelWidth*scale,height=pixelHeight*scale;
  return {x:opening.x+(opening.width-width)/2,y:opening.y+(opening.height-height)/2,width,height};
}
