import { Dot } from "./Dot";

export class Individual {
  public dots: Dot[];
  public fitness: number;
  private width: number;
  private height: number;

  constructor(width: number, height: number, dotCount: number) {
    this.width = width;
    this.height = height;
    this.fitness = 0;
    this.dots = this.initializeDots(dotCount);
  }

  /**
   * Initialize dots with random positions and radii
   */
  private initializeDots(dotCount: number): Dot[] {
    return Array.from(
      { length: dotCount },
      () => new Dot(this.randX(), this.randY(), this.randRadius())
    );
  }

  /**
   * Mutates the individual based on mutation rate
   */
  public mutate(mutationRate: number): void {
    this.dots.forEach((dot) => {
      if (Math.random() < mutationRate) {
        dot.x = this.randX();
      }
      if (Math.random() < mutationRate) {
        dot.y = this.randY();
      }
      if (Math.random() < mutationRate) {
        dot.radius = this.randRadius();
      }
    });
  }

  /**
   * Creates a deep copy of the individual
   */
  public clone(): Individual {
    const clone = new Individual(this.width, this.height, this.dots.length);
    clone.fitness = this.fitness;
    clone.dots = this.dots.map((dot) => dot.clone());
    return clone;
  }

  private randX(): number {
    return Math.floor(Math.random() * this.width);
  }

  private randY(): number {
    return Math.floor(Math.random() * this.height);
  }

  private randRadius(): number {
    return 1 + Math.random() * 2;
  }
}
