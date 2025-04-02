class Population {
  size: number;
  population: Individual[];
  target: ImageData;
  targetData: Uint8Array;
  width: number;
  height: number;
  ctx: CanvasRenderingContext2D;
  constructor(
    size: number,
    target: ImageData,
    ctx: CanvasRenderingContext2D,
    dotCount: number = 50000
  ) {
    this.size = size;
    this.target = target;
    this.population = Array(size);
    this.width = target.width;
    this.height = target.height;
    this.ctx = ctx;

    const targetLength = target.width * target.height;
    this.targetData = new Uint8Array(targetLength);

    for (let i = 0; i < targetLength; i++) {
      this.targetData[i] = target.data[i * 4];
    }

    for (let i = 0; i < size; i++) {
      this.population[i] = new Individual(
        target.width,
        target.height,
        dotCount
      );
    }
  }

  calculateFitness() {
    // Calculate fitness of all individuals in the population
    for (let individual of this.population) {
      const grid = new Uint8Array(this.width * this.height).fill(255);

      // First represent Individuals as dots on a grid
      for (const dot of individual.dots) {
        this.drawDotToGrid(dot, grid);
      }

      // Now calculate the fitness of the Individual
      individual.fitness = this.calculateGridFitness(grid);
    }
  }

  drawDotToGrid(dot: Dot, grid: Uint8Array) {
    const centerX = Math.floor(dot.x);
    const centerY = Math.floor(dot.y);
    const radius = Math.floor(dot.radius);

    // Skip dots completely outside the canvas
    if (
      centerX + radius < 0 ||
      centerX - radius >= this.width ||
      centerY + radius < 0 ||
      centerY - radius >= this.height
    ) {
      return;
    }

    // Fast midpoint circle algorithm with horizontal line filling
    let x = 0;
    let y = radius;
    let d = 1 - radius;

    // Draw horizontal lines through the circle for solid fill
    this.drawHorizontalLine(centerY, centerX - radius, centerX + radius, grid);

    while (y > x) {
      if (d < 0) {
        d += 2 * x + 3;
      } else {
        d += 2 * (x - y) + 5;
        y--;
      }
      x++;

      // Draw horizontal lines at each level
      this.drawHorizontalLine(centerY + y, centerX - x, centerX + x, grid);
      this.drawHorizontalLine(centerY - y, centerX - x, centerX + x, grid);
      this.drawHorizontalLine(centerY + x, centerX - y, centerX + y, grid);
      this.drawHorizontalLine(centerY - x, centerX - y, centerX + y, grid);
    }
  }

  drawHorizontalLine(
    y: number,
    startX: number,
    endX: number,
    grid: Uint8Array
  ) {
    // Skip lines outside canvas
    if (y < 0 || y >= this.height) return;

    // Clip x coordinates to canvas boundaries
    startX = Math.max(0, startX);
    endX = Math.min(this.width - 1, endX);

    // Fill the horizontal line
    for (let x = startX; x <= endX; x++) {
      grid[y * this.width + x] = 0; // Set to black
    }
  }


  // Calculates the fitness of dots on a grid
  calculateGridFitness(grid: Uint8Array): number {
    const totalPixels = this.width * this.height;
    let diff = 0;

    // Simply calculates the difference between the grid and target
    for (let i = 0; i < totalPixels; i++) {
      const pixelDiff = grid[i] - this.targetData[i];
      diff += pixelDiff * pixelDiff;
    }

    const maxPossibleDiff = totalPixels * 255 * 255;
    const rawFitness = 1 - diff / maxPossibleDiff;

    return Math.pow(rawFitness, 0.5);
  }

  // Draw an individual on the canvas. Useful for rendering to the browser
  drawIndividual(dots: Dot[], ctx: CanvasRenderingContext2D) {
    if (!ctx) return null;

    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);

    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);

    ctx.fillStyle = "black";

    // Draw each dot in the Individual representation
    for (let i = 0; i < dots.length; i++) {
      ctx.beginPath();
      ctx.arc(dots[i].x, dots[i].y, dots[i].radius, 0, endAngle);
      ctx.fill();
    }

    return ctx;
  }

  // Get the index of the fittest Individual in the Population
  fittestIndividual() {
    let fittest = 0;

    this.population.forEach(
      (individual, index) =>
        (fittest =
          this.population[fittest].fitness > individual.fitness
            ? fittest
            : index)
    );

    return fittest;
  }
}
