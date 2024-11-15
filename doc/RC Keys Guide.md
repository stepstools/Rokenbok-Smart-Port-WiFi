
# Rokenbok RC Select Keys

## Overview

There are three main components of the selector keys used with the radio controlled Rokenbok system:

1. The LED and metal contacts on the side of the key.
    - Current flows from one contact, through the LED (emitting some photons along the way), and back to ground at the other contact.

2. The two central ridges on the back of the key.
    - The metal bars connecting these ridges are used as a sort of switch that closes the connection between the corresponding metal buttons inside the key socket on the vehicle. This is how the machine knows when a key has been inserted, removed, or pressed down to wake the vehicle.

3. The remaining four ridges (or lack thereof) on the back of the key.
    - These ridges are used as binary code to identify which key has been inserted into the vehicle. This is where the magic happens…

## Binary and Rokenbok

If you know absolutely nothing about binary numbering, I recommend you watch some videos online. You’ll be able to pick it up pretty quickly. As mentioned before, the 4 outer ridges on the RC keys are each one bit. This means that there are 2<sup>4</sup> possible combinations for the ridges and therefore 16 unique control keys. In case you were curious, 4 bits is half of a byte and is therefore referred to as a _nibble_. For reference, here are the equivalent binary, hex, and decimal values for each possible number in a nibble:

| **Binary** | **Hexadecimal** | **Decimal** | **Key Number** |
| --- | --- | --- | --- |
| 0000 | 0   | 0   | 1   |
| 0001 | 1   | 1   | 2   |
| 0010 | 2   | 2   | 3   |
| 0011 | 3   | 3   | 4   |
| 0100 | 4   | 4   | 5   |
| 0101 | 5   | 5   | 6   |
| 0110 | 6   | 6   | 7   |
| 0111 | 7   | 7   | 8   |
| 1000 | 8   | 8   | 9   |
| 1001 | 9   | 9   | 10  |
| 1010 | A   | 10  | 11  |
| 1011 | B   | 11  | 12  |
| 1100 | C   | 12  | 13  |
| 1101 | D   | 13  | 14  |
| 1110 | E   | 14  | 15  |
| 1111 | F   | 15  | No Select |

Okay, so what do nibbles have to do with Rokenbok? I’m glad you asked. When you insert a key into the socket of a vehicle, the 4 outer ridges either depress one of the metal switches or they don’t. If there is a ridge, the switch is depressed. If there isn’t a ridge, the switch is not depressed. Rokenbok decided to be a bit counterintuitive with how they set up this numbering system, so it takes some explaining:

- Because most people don’t start counting at zero, the actual key number is the binary number plus one (this can be seen in the previous table).

  - Also, one value needed to be assigned as a “no selection” value, so 1111 was chosen for this. If the smart port is used to force the selection of a physical controller to decimal number 15, the selection LEDs will spin in a circle as if the controller was asleep.

- The presence of a ridge (and therefore a pressed switch) is a binary zero. The absence of a ridge (and therefore an unpressed switch) is a binary one.

  - This is clearly seen on the number 1 key, which has all four ridges present, therefore corresponding to binary number 0000.

- The ridges are not in any specific order. The pictures below show the order of the ridges starting from bit 0 (all the way right in a nibble) to bit 3 (all the way left in a nibble).

  - This can be clearly seen in the number 8 key, which only has ridge 3 remaining, corresponding to binary number 0111.

![Rokenbok key diagram](images/keys.png)

## Controlling More Than Eight Vehicles

Rokenbok decided to be boring and only use 8 of the 16 possible keys. They actually described potentially making controllers capable of selecting 16 vehicles in their patent requests, but they never got around to doing it. However, the smart port is more than capable of controlling these extra vehicles! Unfortunately, number 16 is unavailable due to its use as the “no select” code, but we can still get numbers 9-15! If you’ve been following along, you’ll know that control keys 1-8 all have ridge three present. Now, if we remove that ridge, suddenly bit number 3 changes from a zero to a one. A one in binary position three is equal to 8, so you’ve just added 8 to the value of the key! Number 1 becomes 9, 2 becomes 10, etc. This checks out with the table above if you flip the leftmost bit of any number. Don’t forget that there’s no need to hack up your number 8 key since 16 isn’t a valid selection.

~~Caveat: I haven’t actually done this. However, I had a friend help me and depress the switch for bit zero on a vehicle using a small screwdriver while I used a piece of wire to simulate the central ridges turning on the vehicle. I was successfully able to control the vehicle as key number 15 (binary 1110, only ridge zero present). It might be possible that only certain vehicles work with this hack. My worst case scenario guess is that only older vehicles work with this. It’s possible that newer vehicles lost this functionality once Rokenbok ditched the smart port and decided not to make 16 selection controllers. Of course, my best case scenario guess is that they all work! Only one way to find out…~~

Update: Multiple people have successfully created keys 9-15 by physically removing the third ridge from keys 1-7.  Key 8 cannot be turned into key 16.

Guide By: Stepstools, the Smart Port Dude
