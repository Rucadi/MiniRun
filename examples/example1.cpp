// This is an adaptation of: https://github.com/mghazaryan/pplx
// In this example, we can see the how we define the different dependences and the lifetime of the MiniRun runtime.


#include "MiniRun.hpp"

#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <numeric>

int calcDotProduct(const std::vector<int>& a, const std::vector<int>& b)
{
  return std::inner_product(a.begin(), a.end(), b.begin(), 0);
}

double calcMagnitude(const std::vector<int>& a)
{
  return std::sqrt(calcDotProduct(a, a));
}

int main()
{
  // To keep things simple let's assume we deal with non-zero vectors only.
  const std::vector<int> v1 { 2, -4, 7 };
  const std::vector<int> v2 { 5, 1, -3 };

  // Form separate tasks for calculating the dot product and magnitudes.
  int v1v2Dot;
  double v1Magnitude,v2Magnitude, result;

{
    MiniRun run(4);
    run.createTask([&](){ v1v2Dot = calcDotProduct(v1,v2);},{},MiniRun::deps(v1v2Dot));
    run.createTask([&](){ v1Magnitude = calcMagnitude(v1);},{},MiniRun::deps(v1Magnitude));
    run.createTask([&](){ v2Magnitude = calcMagnitude(v2);},{},MiniRun::deps(v2Magnitude));
    run.createTask([&](){ result = std::acos((double)v1v2Dot / (v1Magnitude * v2Magnitude)); },MiniRun::deps(v1v2Dot, v1Magnitude,v2Magnitude),{});
    //implicit taskwait at destruction
}
   
  std::cout << "Angle between the vectors: "
            << result
            << " radians."
            << std::endl;

  return 0;
}