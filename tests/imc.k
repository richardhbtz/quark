// EXPECT: Weight: 70.500000 kg
// EXPECT: Height: 1.750000 m
// EXPECT: BMI: 23.020409 kg/m2
int main() 
{
    float weight = 70.5;
    float height = 1.75;
    float result = weight / (height * height);
    print("Weight: {} kg", weight);
    print("Height: {} m", height);
    print("BMI: {} kg/m2", result);

    ret 0;
}