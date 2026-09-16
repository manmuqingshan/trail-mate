// Source rect grid: x=748.71 + column*53.02; y=1147.19 + row*66.27.
// Keep source coordinates: overlay viewBox starts at (696.3,500.37).
export const tdeckKeys = [
  ['Q','W','E','R','T','Y','U','I','O','P'],
  ['A','S','D','F','G','H','J','K','L','BACKSPACE'],
  ['ALT','Z','X','C','V','B','N','M','$','ENTER'],
].flatMap((row,r)=>row.map((key,c)=>({
  key, x:748.71+c*53.02, y:1147.19+r*66.27, width:53.02,height:52.62,
})));
