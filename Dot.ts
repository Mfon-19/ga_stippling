// x, y, and radius are randomized
// 0 <= x <= 500, 0 <= y <= 500, 1 <= radius <= 20
class Dot{
  radius: number;
  y: number;
  x: number;
  constructor(x: number, y: number, radius: number) {
    this.x = x,
    this.y = y,
    this.radius = radius
  }
}