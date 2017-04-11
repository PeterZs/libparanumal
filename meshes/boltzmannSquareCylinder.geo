cl__1 = 1;
Point(1) = {-5, -3, 0, 0.5};
Point(2) = {15, -3, 0, 0.5};
Point(3) = {15, 3, 0, 0.5};
Point(4) = {-5, 3, 0, 0.5};
Point(5) = {-0.25, -0.25, 0, .1};
Point(6) = {0.25, -0.25, 0, .1};
Point(7) = {0.25, 0.25, 0, .1};
Point(8) = {-0.25, 0.25, 0, .1};
Line(1) = {1, 2};
Line(2) = {2, 3};
Line(3) = {3, 4};
Line(4) = {4, 1};
Line(5) = {5, 6};
Line(6) = {6, 7};
Line(7) = {7, 8};
Line(8) = {8, 5};
Line Loop(11) = {4, 1, 2, 3, -7, -6, -5, -8};
Plane Surface(11) = {11};
Physical Line("Wall", 1) = {5, 6, 7, 8};
Physical Line("Inflow", 2) = {4};
Physical Line("Outflow", 3) = {2};
Physical Line("Slip", 4) = {1,3};

Coherence;
Line Loop(12) = {4, 1, 2, 3};
Line Loop(13) = {8, 5, 6, 7};
Plane Surface(14) = {12, 13};
Physical Surface("Domain") = {11};
