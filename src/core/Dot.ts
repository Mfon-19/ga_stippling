import { Point } from "../types";

export class Dot implements Point {
  constructor(public x: number, public y: number, public radius: number) {}

  /**
   * Creates a copy of the dot
   */
  public clone(): Dot {
    return new Dot(this.x, this.y, this.radius);
  }
}
