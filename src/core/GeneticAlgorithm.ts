import { Population, PopulationConfig } from "./Population";
import { Individual } from "./Individual";
import { Dot } from "./Dot";

export interface GeneticAlgorithmConfig {
  populationSize: number;
  mutationRate: number;
  dotCount: number;
  elitismRatio: number;
}

export class GeneticAlgorithm {
  private population: Population;
  private config: GeneticAlgorithmConfig;
  private target: ImageData;

  constructor(target: ImageData, config: GeneticAlgorithmConfig) {
    this.target = target;
    this.config = config;

    const popConfig: PopulationConfig = {
      size: config.populationSize,
      width: target.width,
      height: target.height,
      dotCount: config.dotCount,
    };

    this.population = new Population(popConfig, target);
  }

  /**
   * Perform one generation of evolution
   */
  public evolve(): void {
    this.population.calculateFitness();
    this.createNewGeneration();
  }

  /**
   * Create new generation through selection and mutation
   */
  private createNewGeneration(): void {
    const elitismCount = Math.floor(
      this.config.populationSize * this.config.elitismRatio
    );
    const newPopulation: Individual[] = this.preserveElites(elitismCount);

    while (newPopulation.length < this.config.populationSize) {
      const { parent1, parent2 } = this.selectParents();
      const child = this.crossover(parent1, parent2);
      child.mutate(this.config.mutationRate);
      newPopulation.push(child);
    }

    this.population.population = newPopulation;
  }

  /**
   * Preserve elite individuals
   */
  private preserveElites(count: number): Individual[] {
    return [...this.population.population]
      .sort((a, b) => b.fitness - a.fitness)
      .slice(0, count)
      .map((individual) => individual.clone());
  }

  /**
   * Select parents using roulette wheel selection
   */
  private selectParents(): { parent1: Individual; parent2: Individual } {
    const totalFitness = this.population.population.reduce(
      (sum, ind) => sum + ind.fitness,
      0
    );

    const selectParent = () => {
      let threshold = Math.random() * totalFitness;
      let sum = 0;

      for (const individual of this.population.population) {
        sum += individual.fitness;
        if (sum >= threshold) {
          return individual;
        }
      }

      return this.population.population[this.population.population.length - 1];
    };

    return {
      parent1: selectParent(),
      parent2: selectParent(),
    };
  }

  /**
   * Perform crossover between two parents
   */
  private crossover(parent1: Individual, parent2: Individual): Individual {
    const child = new Individual(
      this.target.width,
      this.target.height,
      parent1.dots.length
    );

    for (let i = 0; i < parent1.dots.length; i++) {
      const fitnessA = this.calculateDotFitness(parent1.dots[i]);
      const fitnessB = this.calculateDotFitness(parent2.dots[i]);

      child.dots[i] = (fitnessA > fitnessB ? parent1 : parent2).dots[i].clone();
    }

    return child;
  }

  /**
   * Calculate fitness contribution of a single dot
   */
  private calculateDotFitness(dot: Dot): number {
    const x = Math.floor(dot.x);
    const y = Math.floor(dot.y);

    if (x < 0 || x >= this.target.width || y < 0 || y >= this.target.height) {
      return 0;
    }

    const pixelIndex = (y * this.target.width + x) * 4;
    return 255 - this.target.data[pixelIndex];
  }

  /**
   * Get current population
   */
  public getPopulation(): Population {
    return this.population;
  }
}