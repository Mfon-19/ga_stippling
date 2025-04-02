class Individual {
  dots: Dot[];
  fitness: number = 0;
  width: number;
  height: number;
  constructor(width: number, height: number, dotCount: number = 50000) {
    this.width = width;
    this.height = height;
    this.dots = new Array(dotCount);

    for (let i = 0; i < dotCount; i++) {
      this.dots[i] = new Dot(this.randX(), this.randY(), this.randRadius());
    }
  }

  mutate(mutationRate: number) {
    // Generate new random values for some the x, y, and radius values
    for (let i = 0; i < this.dots.length; i++) {
      this.dots[i].x =
        Math.random() < mutationRate ? this.randX() : this.dots[i].x;
      this.dots[i].y =
        Math.random() < mutationRate ? this.randY() : this.dots[i].y;
      this.dots[i].radius =
        Math.random() < mutationRate ? this.randRadius() : this.dots[i].radius;
    }
  }

  // Random x value
  private randX() {
    return Math.floor(Math.random() * this.width);
  }

  // Random y value
  private randY() {
    return Math.floor(Math.random() * this.height);
  }

  // Random radius value
  private randRadius() {
    return 1 + Math.random() * 2;
  }
}
