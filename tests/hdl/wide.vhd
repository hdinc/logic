-- a 100 bit adder and a 4 bit bus driver
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity wide is
    port (
        a, b   : in    std_logic_vector(99 downto 0);
        sum    : out   std_logic_vector(99 downto 0);
        oe     : in    std_logic;
        d      : in    std_logic_vector(3 downto 0);
        bus_io : inout std_logic_vector(3 downto 0);
        seen   : out   std_logic_vector(3 downto 0)
    );
end entity;

architecture rtl of wide is
begin
    sum <= std_logic_vector(unsigned(a) + unsigned(b));
    bus_io <= d when oe = '1' else (others => 'Z');
    seen <= bus_io;
end architecture;
