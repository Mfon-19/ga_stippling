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

  crossover(a: Individual, b: Individual) {
    // Cross over x, y, and radius values of two individuals
    let child = new Individual(a.width, a.height);
    for (let i = 0; i < 10000; i++) {
      // 50% chance of choosing either parent's dot
      const x = Math.random() < 0.5 ? a.dots[i].x : b.dots[i].x;
      const y = Math.random() < 0.5 ? a.dots[i].y : b.dots[i].y;
      const radius = Math.random() < 0.5 ? a.dots[i].radius : b.dots[i].radius;
      child.dots[i] = new Dot(x, y, radius);
    }

    return child;
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
