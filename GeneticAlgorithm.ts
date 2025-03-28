// The main Genetic Algorithm

class GeneticAlgorithm {
  popSize: number;
  mutationRate: number;
  population: Population;
  constructor(
    popSize: number,
    mutationRate: number,
    target: ImageData,
    ctx: CanvasRenderingContext2D
  ) {
    this.popSize = popSize;
    this.mutationRate = mutationRate;
    this.population = new Population(popSize, target, ctx);
  }

  evolve() {
    // Run a generation
    this.population.calculateFitness();
    this.newGeneration();
  }

  // RANDOM CROSSOVER WAS DUMBBBB!!!!!!!!!!!!!!
  crossover(a: Individual, b: Individual) {
    // Cross over x, y, and radius values of two individuals
    let child = new Individual(a.width, a.height);

    const target = this.population.target;

    for (let i = 0; i < 10000; i++) {
      // Calculate which parent's dot better represents the target at this position
      const fitnessA = this.calculateDotFitness(a.dots[i], target);
      const fitnessB = this.calculateDotFitness(b.dots[i], target);

      if (fitnessA > fitnessB) {
        child.dots[i] = new Dot(a.dots[i].x, a.dots[i].y, a.dots[i].radius);
      } else {
        child.dots[i] = new Dot(b.dots[i].x, b.dots[i].y, b.dots[i].radius);
      }
    }

    return child;
  }

  // Helper method to calculate how well a dot matches the target
  calculateDotFitness(dot: Dot, target: ImageData): number {
    // For a black and white image, we would check:
    // 1. Is the dot positioned on a black pixel in the target?
    // 2. How well does the dot's size cover the black area?

    const x = Math.floor(dot.x);
    const y = Math.floor(dot.y);

    // Check if position is valid
    if (x < 0 || x >= target.width || y < 0 || y >= target.height) {
      return 0; // Out of bounds
    }

    // Get pixel value (for grayscale, we only need one channel)
    const pixelIndex = (y * target.width + x) * 4;
    const pixelValue = target.data[pixelIndex]; // 0 (black) to 255 (white)

    // If pixel is black (or close to black), dot is well-positioned
    const positionScore = 255 - pixelValue; // Higher for darker pixels

    // Additional scoring based on how well the dot's radius matches the black region
    // This would require more complex analysis of surrounding pixels

    return positionScore;
  }

  parentSelect(): { parent1: Individual; parent2: Individual } {
    // Calculate total fitness for better selection probability
    const totalFitness = this.population.population.reduce(
      (sum, ind) => sum + ind.fitness,
      0
    );

    // Use fitness proportionate selection (roulette wheel)
    const selectParent = () => {
      const threshold = Math.random() * totalFitness;
      let sum = 0;

      for (const individual of this.population.population) {
        sum += individual.fitness;
        if (sum >= threshold) {
          return individual;
        }
      }

      // Fallback to last individual (should rarely happen)
      return this.population.population[this.population.population.length - 1];
    };

    return {
      parent1: selectParent(),
      parent2: selectParent(),
    };
  }

  newGeneration() {
    const elitismCount = Math.floor(this.popSize * 0.1); // Keep top 10%
    let newPopulation = [];

    // Sort population by fitness (descending)
    const sortedPop = [...this.population.population].sort(
      (a, b) => b.fitness - a.fitness
    );

    // Keep the best individuals (elitism)
    for (let i = 0; i < elitismCount; i++) {
      newPopulation.push(sortedPop[i]);
    }

    // Fill the rest with children from crossover and mutation
    while (newPopulation.length < this.popSize) {
      const { parent1, parent2 } = this.parentSelect();
      if (!parent1 || !parent2) throw new Error("No parents selected!");
      let child = this.crossover(parent1 as Individual, parent2 as Individual);
      child.mutate(this.mutationRate);
      newPopulation.push(child);
    }

    this.population.population = newPopulation;
  }
}
