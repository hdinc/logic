-- y follows a 5 ns later, done is 1 from 20 ns
library ieee;
use ieee.std_logic_1164.all;

entity delay is
    port (
        a    : in  std_logic;
        y    : out std_logic := '0';
        done : out std_logic := '0'
    );
end entity;

architecture sim of delay is
begin
    y <= transport a after 5 ns;
    done <= '1' after 20 ns;
end architecture;
