import { Individual } from "./Individual";
import { Dot } from "./Dot";

export interface PopulationConfig {
  size: number;
  width: number;
  height: number;
  dotCount: number;
}

export class Population {
  public population: Individual[];
  private targetData: Uint8Array;
  private width: number;
  private height: number;

  constructor(config: PopulationConfig, target: ImageData) {
    this.width = config.width;
    this.height = config.height;
    this.population = this.initializePopulation(config);
    this.targetData = this.prepareTargetData(target);
  }

  /**
   * Initialize population with random individuals
   */
  private initializePopulation(config: PopulationConfig): Individual[] {
    return Array.from(
      { length: config.size },
      () => new Individual(config.width, config.height, config.dotCount)
    );
  }

  /**
   * Prepare target data for fitness calculations
   */
  private prepareTargetData(target: ImageData): Uint8Array {
    const targetLength = target.width * target.height;
    const targetData = new Uint8Array(targetLength);

    for (let i = 0; i < targetLength; i++) {
      targetData[i] = target.data[i * 4];
    }

    return targetData;
  }

  /**
   * Calculate fitness for all individuals
   */
  public calculateFitness(): void {
    this.population.forEach((individual) => {
      const grid = this.createGrid();
      this.drawDotsToGrid(individual.dots, grid);
      individual.fitness = this.calculateGridFitness(grid);
    });
  }

  /**
   * Create empty grid for fitness calculation
   */
  private createGrid(): Uint8Array {
    return new Uint8Array(this.width * this.height).fill(255);
  }

  /**
   * Draw dots to grid for fitness calculation
   */
  private drawDotsToGrid(dots: Dot[], grid: Uint8Array): void {
    dots.forEach((dot) => {
      this.drawDotToGrid(dot, grid);
    });
  }

  /**
   * Draw single dot to grid using midpoint circle algorithm
   */
  private drawDotToGrid(dot: Dot, grid: Uint8Array): void {
    const centerX = Math.floor(dot.x);
    const centerY = Math.floor(dot.y);
    const radius = Math.floor(dot.radius);

    if (this.isOutsideCanvas(centerX, centerY, radius)) return;

    this.drawCircle(centerX, centerY, radius, grid);
  }

  private isOutsideCanvas(x: number, y: number, radius: number): boolean {
    return (
      x + radius < 0 ||
      x - radius >= this.width ||
      y + radius < 0 ||
      y - radius >= this.height
    );
  }

  /**
   * Draw circle using midpoint circle algorithm
   */
  private drawCircle(
    centerX: number,
    centerY: number,
    radius: number,
    grid: Uint8Array
  ): void {
    let x = 0;
    let y = radius;
    let d = 1 - radius;

    this.drawHorizontalLine(centerY, centerX - radius, centerX + radius, grid);

    while (y > x) {
      if (d < 0) {
        d += 2 * x + 3;
      } else {
        d += 2 * (x - y) + 5;
        y--;
      }
      x++;

      this.drawHorizontalLine(centerY + y, centerX - x, centerX + x, grid);
      this.drawHorizontalLine(centerY - y, centerX - x, centerX + x, grid);
      this.drawHorizontalLine(centerY + x, centerX - y, centerX + y, grid);
      this.drawHorizontalLine(centerY - x, centerX - y, centerX + y, grid);
    }
  }

  /**
   * Draw horizontal line with boundary checking
   */
  private drawHorizontalLine(
    y: number,
    startX: number,
    endX: number,
    grid: Uint8Array
  ): void {
    if (y < 0 || y >= this.height) return;

    startX = Math.max(0, startX);
    endX = Math.min(this.width - 1, endX);

    for (let x = startX; x <= endX; x++) {
      grid[y * this.width + x] = 0;
    }
  }

  /**
   * Calculate fitness based on difference from target
   */
  private calculateGridFitness(grid: Uint8Array): number {
    const totalPixels = this.width * this.height;
    let diff = 0;

    for (let i = 0; i < totalPixels; i++) {
      const pixelDiff = grid[i] - this.targetData[i];
      diff += pixelDiff * pixelDiff;
    }

    const maxPossibleDiff = totalPixels * 255 * 255;
    const rawFitness = 1 - diff / maxPossibleDiff;

    return Math.pow(rawFitness, 0.5);
  }

  /**
   * Get index of fittest individual
   */
  public getFittestIndex(): number {
    return this.population.reduce(
      (maxIndex, current, currentIndex, arr) =>
        current.fitness > arr[maxIndex].fitness ? currentIndex : maxIndex,
      0
    );
  }

  /**
   * Draw individual to canvas context
   */
  public drawIndividual(dots: Dot[], ctx: CanvasRenderingContext2D): void {
    ctx.clearRect(0, 0, this.width, this.height);
    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, this.width, this.height);
    ctx.fillStyle = "black";

    dots.forEach((dot) => {
      ctx.beginPath();
      ctx.arc(dot.x, dot.y, dot.radius, 0, 2 * Math.PI);
      ctx.fill();
    });
  }
}
